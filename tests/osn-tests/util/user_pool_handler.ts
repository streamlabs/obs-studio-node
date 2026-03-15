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
    platforms: any;
}

export class UserPoolHandler {
    private user: ITestUser;
    private userPoolUrl: string = 'https://slobs-users-pool.herokuapp.com/';
    private osnTestName: string;

    constructor(testName: string) {
        this.osnTestName = testName;
    }

    private async requestUser(): Promise<any> {
        const res = await fetch(this.userPoolUrl + 'reserve/twitch', {
            headers: { Authorization: `Bearer: ${process.env.SLOBS_TEST_USER_POOL_TOKEN}` },
        });

        if (!res.ok) {
            logWarning(this.osnTestName, 'Request user got status ' + res.status);
            throw new Error(`Unable to request user, status ${res.status}`);
        }

        return res.json();
    }

    async getStreamKey(): Promise<string> {
        let attempt: number = 1;
        let totalAttempts: number = 3;

        while(attempt <= totalAttempts) {
            try {
                logInfo(this.osnTestName, 'Requesting user from pool ('+ attempt + '/' + totalAttempts + ')');
                this.user = await this.requestUser();
                break;
            } catch(e) {
                if (attempt) {
                    await sleep(20000);
                }
            }

            attempt++;
        }

        if (!this.user) {
            throw 'Unable to get user from pool.';
        }

        logInfo(this.osnTestName, 'Got user ' + this.user.email);
        return this.user.platforms.twitch.streamKey;
    }

    async releaseUser() {
        const res = await fetch(this.userPoolUrl + `release/${this.user.type}/${this.user.email}`, {
            headers: { Authorization: `Bearer: ${process.env.SLOBS_TEST_USER_POOL_TOKEN}` },
        });

        if (!res.ok) {
            throw new Error(`Unable to release user, status ${res.status}`);
        }

        return res.json();
    }
}