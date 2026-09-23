import { sleep } from './general';
import { logInfo, logWarning } from './logger';


type TPlatform = 'twitch' | 'youtube' | 'mixer' | 'facebook';

interface ITestUser {
    email: string;
    workerId: string;
    updated: string;
    enabled: boolean;
    type: TPlatform;
    username: string;
    id: string;
    token: string;
    apiToken: string;
    widgetToken: string;
    streamKey: string;
    platforms: Record<string, { streamKey: string }>;
}

export class InvalidUserPoolResponseError extends Error {
    constructor(message: string) {
        super(message);
        this.name = 'InvalidUserPoolResponseError';
        Object.setPrototypeOf(this, InvalidUserPoolResponseError.prototype);
    }
}

export class UserPoolHandler {
    private static excludedUsers: Set<string> = new Set();
    private user: ITestUser | null = null;
    private userPoolUrl: string = 'https://slobs-users-pool.herokuapp.com/';
    private osnTestName: string;

    constructor(testName: string) {
        this.osnTestName = testName;
    }

    private isUserExcluded(email: string): boolean {
        return UserPoolHandler.excludedUsers.has(email);
    }

    private excludeUser(email: string, reason: string) {
        UserPoolHandler.excludedUsers.add(email);
        logWarning(this.osnTestName, `Marking user ${email} as unhealthy: ${reason}`);
    }

    private async requestUser(): Promise<ITestUser> {
        const res = await fetch(this.userPoolUrl + 'reserve/twitch', {
            headers: { Authorization: `Bearer: ${process.env.SLOBS_TEST_USER_POOL_TOKEN}` },
        });

        if (!res.ok) {
            logWarning(this.osnTestName, 'Request user got status ' + res.status);
            throw new Error(`Unable to request user, status ${res.status}`);
        }

        return await res.json() as ITestUser;
    }

    private async releaseReservedUser(user: ITestUser): Promise<unknown> {
        const res = await fetch(this.userPoolUrl + `release/${user.type}/${user.email}`, {
            headers: { Authorization: `Bearer: ${process.env.SLOBS_TEST_USER_POOL_TOKEN}` },
        });

        if (!res.ok) {
            throw new Error(`Unable to release user, status ${res.status}`);
        }

        return res.json();
    }

    async getStreamKey(): Promise<string> {
        const totalAttempts = 3;
        const releaseTypes: TPlatform[] = ['twitch', 'youtube', 'mixer', 'facebook'];
        let sawInvalidUser = false;
        let lastRequestError: unknown;

        for (let attempt = 1; attempt <= totalAttempts; attempt++) {
            let reservedUser: ITestUser;
            try {
                logInfo(this.osnTestName, 'Requesting user from pool ('+ attempt + '/' + totalAttempts + ')');
                reservedUser = await this.requestUser();
            } catch(e) {
                lastRequestError = e;
                if (attempt < totalAttempts) {
                    await sleep(20000);
                }
                continue;
            }

            if (!reservedUser || typeof reservedUser.email !== 'string' || !reservedUser.email
                || releaseTypes.indexOf(reservedUser.type) < 0) {
                throw new InvalidUserPoolResponseError('User pool returned a reservation without a usable release identity.');
            }

            const streamKey = reservedUser.platforms?.twitch?.streamKey;
            const excluded = this.isUserExcluded(reservedUser.email);
            if (excluded || reservedUser.type !== 'twitch' || typeof streamKey !== 'string' || !streamKey.trim()) {
                if (!excluded) sawInvalidUser = true;
                logWarning(this.osnTestName, `Discarding ${excluded ? 'excluded' : 'invalid'} user ${reservedUser.email} from pool response`);
                try {
                    await this.releaseReservedUser(reservedUser);
                } catch (releaseError) {
                    throw new InvalidUserPoolResponseError(`Unable to release rejected user ${reservedUser.email}: ${releaseError}`);
                }
                if (attempt < totalAttempts) await sleep(2000);
                continue;
            }

            this.user = reservedUser;
            logInfo(this.osnTestName, 'Got user ' + reservedUser.email);
            return streamKey;
        }

        if (sawInvalidUser) {
            throw new InvalidUserPoolResponseError('Unable to get a Twitch user with a stream key from the pool.');
        }
        throw new Error(lastRequestError
            ? `Unable to get user from pool: ${lastRequestError}`
            : 'Unable to get user from pool: no available accounts.');
    }

    markCurrentUserUnhealthy(reason: string) {
        if (!this.user) {
            return;
        }

        this.excludeUser(this.user.email, reason);
    }

    async releaseUser(): Promise<unknown> {
        if (!this.user) {
            return null;
        }

        try {
            return await this.releaseReservedUser(this.user);
        } finally {
            this.user = null;
        }
    }
}
