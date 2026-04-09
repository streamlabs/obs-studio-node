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
        let attempt: number = 1;
        let totalAttempts: number = 3;

        while(attempt <= totalAttempts) {
            try {
                logInfo(this.osnTestName, 'Requesting user from pool ('+ attempt + '/' + totalAttempts + ')');
                const reservedUser = await this.requestUser();

                if (this.isUserExcluded(reservedUser.email)) {
                    logWarning(this.osnTestName, `Discarding excluded user ${reservedUser.email} from pool response`);

                    try {
                        await this.releaseReservedUser(reservedUser);
                    } catch (releaseError) {
                        logWarning(this.osnTestName, `Unable to release excluded user ${reservedUser.email}: ${releaseError}`);
                    }

                    if (attempt < totalAttempts) {
                        await sleep(2000);
                    }

                    attempt++;
                    continue;
                }

                this.user = reservedUser;
                break;
            } catch(e) {
                if (attempt < totalAttempts) {
                    await sleep(20000);
                }
            }

            attempt++;
        }

        if (!this.user) {
            throw new Error('Unable to get user from pool.');
        }

        logInfo(this.osnTestName, 'Got user ' + this.user.email);
        return this.user.platforms.twitch.streamKey;
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
