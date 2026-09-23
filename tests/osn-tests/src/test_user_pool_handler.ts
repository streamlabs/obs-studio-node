import 'mocha';
import { expect } from 'chai';
import { InvalidUserPoolResponseError, UserPoolHandler } from '../util/user_pool_handler';

describe('user pool handler', () => {
    const originalFetch = global.fetch;

    afterEach(() => {
        global.fetch = originalFetch;
    });

    function poolResponse(user: object): Response {
        return { ok: true, json: async () => user } as Response;
    }

    it('releases a reservation without a Twitch key and retries', async () => {
        const requests: string[] = [];
        let reservations = 0;
        global.fetch = (async (url: string) => {
            requests.push(url);
            if (url.endsWith('/reserve/twitch')) {
                reservations++;
                return poolResponse(reservations === 1
                    ? { email: 'invalid@example.com', type: 'twitch' }
                    : { email: 'valid@example.com', type: 'twitch', platforms: { twitch: { streamKey: 'valid-key' } } });
            }
            return poolResponse({});
        }) as typeof fetch;

        const handler = new UserPoolHandler('user-pool-test');
        expect(await handler.getStreamKey()).to.equal('valid-key');
        await handler.releaseUser();
        expect(requests.filter(url => url.endsWith('/reserve/twitch'))).to.have.length(2);
        expect(requests.some(url => url.endsWith('/release/twitch/invalid@example.com'))).to.equal(true);
        expect(requests.some(url => url.endsWith('/release/twitch/valid@example.com'))).to.equal(true);
    });

    it('reports repeated invalid reservations after releasing each one', async () => {
        const releases: string[] = [];
        let reservations = 0;
        global.fetch = (async (url: string) => {
            if (url.endsWith('/reserve/twitch')) {
                reservations++;
                return poolResponse({ email: `invalid-${reservations}@example.com`, type: 'twitch' });
            }
            releases.push(url);
            return poolResponse({});
        }) as typeof fetch;

        const handler = new UserPoolHandler('user-pool-test');
        try {
            await handler.getStreamKey();
            expect.fail('Expected invalid reservations to fail');
        } catch (error) {
            expect(error).to.be.instanceOf(InvalidUserPoolResponseError);
            expect((error as Error).message).to.contain('Twitch user with a stream key');
        }
        expect(reservations).to.equal(3);
        expect(releases).to.have.length(3);
    });
});
