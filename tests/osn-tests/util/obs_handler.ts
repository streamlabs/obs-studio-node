import * as osn from '../osn';
import { logInfo, logWarning } from '../util/logger';
import { UserPoolHandler } from './user_pool_handler';
import { CacheUploader } from '../util/cache-uploader'
import { EOBSOutputType, EOBSOutputSignal, EOBSSettingsCategories } from '../util/obs_enums'
import { sleep } from './general';
import { loadTestEnv } from './env';
import { v4 as uuidv4 } from 'uuid';
const WaitQueue = require('wait-queue');
const retryContext = require('./retry_context.ts');

loadTestEnv();

// Interfaces
export interface IPerformanceState {
    CPU: number;
    numberDroppedFrames: number;
    percentageDroppedFrames: number;
    streamingBandwidth: number;
    streamingDataOutput: number;
    recordingBandwidth: number;
    recordingDataOutput: number;
    frameRate: number;
    averageTimeToRenderFrame: number;
    memoryUsage: number;
    diskSpaceAvailable: string;
}

export interface IOBSOutputSignalInfo {
    type: EOBSOutputType;
    signal: EOBSOutputSignal;
    code: osn.EOutputCode;
    error: string;
    service: string;
}

export interface IConfigProgress {
    event: TConfigEvent;
    description: string;
    percentage?: number;
    continent?: string;
}

export interface IVec2 {
    x: number;
    y: number;
}

interface IRetryFailure {
    message?: string;
}

export interface ICrop {
    top: number;
    bottom: number;
    left: number;
    right: number;
}

// Types
export type TOBSHotkey = {
    ObjectName: string;
    ObjectType: osn.EHotkeyObjectType;
    HotkeyName: string;
    HotkeyDesc: string;
    HotkeyId: number;
};

export type TConfigEvent = 'starting_step' | 'progress' | 'stopping_step' | 'error' | 'done';

// OBSHandler class
export class OBSHandler {
    private path = require('path');

    // Variables for obs initialization
    private workingDirectory: string = this.path.normalize(osn.wd);
    private language: string = 'en-US';
    private obsPath: string = this.path.join(this.path.normalize(__dirname), '..', 'osnData/slobs-client');
    private pipeName: string = 'osn-tests-pipe-'.concat(uuidv4());
    private version: string = '0.00.00-preview.0';
    private crashServer: string = '';

    // Other variables/objects
    private userPoolHandler: UserPoolHandler;
    private cacheUploader: CacheUploader;
    private hasUserFromPool: boolean = false;
    private osnTestName: string;
    signals = new WaitQueue();
    private progress = new WaitQueue();
    inputTypes: string[];
    filterTypes: string[];
    transitionTypes: string[];
    os: string;
    ci: boolean;

    userStreamKey: string;
    defaultVideoContext: osn.IVideo;

    constructor(testName: string, needDefaultVideoContext: boolean = true) {
        this.os = process.platform;
        this.osnTestName = testName;
        this.cacheUploader = new CacheUploader(testName, this.obsPath);
        this.ci = process.env.CI === 'true';
        this.startup();
        if (needDefaultVideoContext) {
            this.createDefaultVideoContext();
        }
        this.inputTypes = osn.InputFactory.types();
        const index = this.inputTypes.indexOf('syphon-input', 0);
        if (index > -1) {
            this.inputTypes.splice(index, 1);
        }
        this.filterTypes = osn.FilterFactory.types();
        this.transitionTypes = osn.TransitionFactory.types();
    }

    startup() {
        let initResult: number;
        logInfo(this.osnTestName, 'Initializing OBS');

        try {
            const exitCode = osn.NodeObs.IPC.host(this.pipeName);
            if (exitCode !== osn.EVideoCodes.Success) {
                if (exitCode === osn.EIPCError.OTHER_ERROR) {
                    throw Error(`OBS IPC host failed: missing executable or some other error. Code ${exitCode}`);
                }
                throw Error(`OBS IPC host failed with code ${exitCode}. See osn.EIPCError for more details.`);
            }
            osn.NodeObs.SetWorkingDirectory(this.workingDirectory);
            initResult = osn.NodeObs.OBS_API_initAPI(this.language, this.obsPath, this.version, this.crashServer, this.osnTestName);
        } catch (e) {
            throw Error('Exception when initializing OBS process: ' + e);
        }

        if (initResult !== osn.EVideoCodes.Success) {
            throw Error('OBS process initialization failed with code ' + initResult);
        }

        logInfo(this.osnTestName, 'OBS started successfully');
    }

    shutdown() {
        if(this.defaultVideoContext) {
            this.destroyDefaultVideoContext();
        }

        logInfo(this.osnTestName, 'Shutting down OBS');

        try {
            osn.NodeObs.OBS_service_removeCallback();
            osn.NodeObs.IPC.disconnect();
        } catch (e) {
            throw Error('Exception when shutting down OBS process: ' + e);
        }

        logInfo(this.osnTestName, 'OBS shutdown successfully');
    }

    instantiateUserPool(testName: string) {
        this.userPoolHandler = new UserPoolHandler(testName);
    }

    async reserveUser() {
        this.userStreamKey = "";

        try {
            logInfo(this.osnTestName, 'Getting stream key from user pool');
            this.userStreamKey = await this.userPoolHandler.getStreamKey();
            this.hasUserFromPool = true;
        } catch (e) {
            logWarning(this.osnTestName, e);
            logWarning(this.osnTestName, 'Using predefined stream key');
            this.userStreamKey = process.env.SLOBS_BE_STREAMKEY;
            this.hasUserFromPool = false;
        }

        logInfo(this.osnTestName, 'Saving stream key');
        this.setStreamKey(this.userStreamKey);

        let savedStreamKey = this.getStreamKey();
        if (savedStreamKey === this.userStreamKey) {
            logInfo(this.osnTestName, 'Stream key saved successfully');
        } else {
            throw Error('Failed to save stream key');
        }
    }

    async releaseUser() {
        if (this.hasUserFromPool) {
            await this.userPoolHandler.releaseUser();
            this.hasUserFromPool = false;
        }
    }

    async finalizeRetryableTest(context: Mocha.Context): Promise<boolean> {
        const currentTest = context.currentTest;
        const hasFailed = currentTest?.state === 'failed';

        await this.prepareRetryUserIfNeeded(currentTest);
        return hasFailed;
    }

    private clearOutputSignals() {
        this.signals = new WaitQueue();
    }

    private getRetryFailureMessage(retryFailure?: IRetryFailure | null): string {
        if (!retryFailure || typeof retryFailure.message !== 'string') {
            return '';
        }

        return retryFailure.message;
    }

    private getTestLabel(test?: Mocha.Test): string {
        if (!test) {
            return 'unknown test';
        }

        if (typeof test.fullTitle === 'function') {
            const fullTitle = test.fullTitle();
            if (fullTitle.length > 0) {
                return fullTitle;
            }
        }

        return test.title || 'unknown test';
    }

    private hasRetriesRemaining(test?: Mocha.Test): boolean {
        const runnable = test as any;

        if (!runnable || typeof runnable.currentRetry !== 'function' || typeof runnable.retries !== 'function') {
            return false;
        }

        return runnable.currentRetry() < runnable.retries();
    }

    private getPendingRetryFailure(test?: Mocha.Test): IRetryFailure | null {
        if (!test || !this.hasRetriesRemaining(test)) {
            return null;
        }

        const retryFailure = retryContext.getRetryFailure(test);
        return retryFailure || null;
    }

    private parseOutputErrorCode(message: string): osn.EOutputCode | null {
        const codeMatch = message.match(/Error code:\s*(-?\d+)/i);
        if (!codeMatch) {
            return null;
        }

        const parsedCode = Number(codeMatch[1]);
        return Number.isFinite(parsedCode)
            ? parsedCode as osn.EOutputCode
            : null;
    }

    private shouldRotateUserForRetry(retryFailure: IRetryFailure): string | null {
        if (!this.hasUserFromPool || !this.userPoolHandler) {
            return null;
        }

        const message = this.getRetryFailureMessage(retryFailure);
        if (!message) {
            return null;
        }

        // Only rotate the pooled account when the failure suggests a bad remote
        // session or throttled credentials; most flaky cases can reuse the user
        // once OBS state has been cleaned up.
        const outputErrorCode = this.parseOutputErrorCode(message);
        if (outputErrorCode === osn.EOutputCode.ConnectFailed
            || outputErrorCode === osn.EOutputCode.Disconnected) {
            return `received streaming output error ${outputErrorCode}`;
        }

        const normalizedMessage = message.toLowerCase();
        const retryableTimeouts = [
            'streaming starting signal timeout',
            'streaming activate signal timeout',
            'streaming start signal timeout',
            'recording start signal timeout',
            'recording wrote signal timeout',
            'replay-buffer start signal timeout',
            'replay-buffer writing signal timeout',
        ];

        if (retryableTimeouts.some(timeoutMessage => normalizedMessage.includes(timeoutMessage))) {
            return message;
        }

        return null;
    }

    private async cleanupOutputsForRetry() {
        // The next attempt reuses the same OBS process, so clear both the active
        // outputs and any queued signals that would otherwise bleed into the retry.
        this.clearOutputSignals();

        try {
            osn.NodeObs.OBS_service_stopReplayBuffer(false);
        } catch (e) {}

        try {
            osn.NodeObs.OBS_service_stopRecording();
        } catch (e) {}

        try {
            osn.NodeObs.OBS_service_stopStreaming(false);
        } catch (e) {}

        await sleep(1000);
        this.clearOutputSignals();
    }

    private restoreKnownGoodStreamKey() {
        if (!this.userStreamKey) {
            return;
        }

        logInfo(this.osnTestName, 'Restoring stream key before retry');
        this.setStreamKey(this.userStreamKey);

        if (this.getStreamKey() !== this.userStreamKey) {
            throw new Error('Failed to restore stream key before retry');
        }

        logInfo(this.osnTestName, 'Stream key restored before retry');
    }

    async prepareRetryUserIfNeeded(test?: Mocha.Test): Promise<void> {
        // Tests call this from afterEach so we can recover before Mocha starts the
        // next attempt. The retry_context store tells us why the previous attempt failed.
        const retryFailure = this.getPendingRetryFailure(test);
        if (!retryFailure) {
            return;
        }

        const testLabel = this.getTestLabel(test);
        const failureMessage = this.getRetryFailureMessage(retryFailure);
        const replacementReason = this.shouldRotateUserForRetry(retryFailure);

        try {
            logWarning(this.osnTestName, `Preparing retry for "${testLabel}" after: ${failureMessage}`);
            await this.cleanupOutputsForRetry();

            if (replacementReason) {
                logWarning(this.osnTestName, `Replacing user pool account before retrying "${testLabel}": ${replacementReason}`);
                this.userPoolHandler.markCurrentUserUnhealthy(`retrying "${testLabel}" after ${replacementReason}`);
                await this.releaseUser();
                await this.reserveUser();
                logInfo(this.osnTestName, `Prepared fresh stream credentials for retrying "${testLabel}"`);
            } else {
                this.restoreKnownGoodStreamKey();
                logInfo(this.osnTestName, `Prepared clean OBS state for retrying "${testLabel}"`);
            }

            this.clearOutputSignals();
        } catch (e) {
            logWarning(this.osnTestName, `Unable to prepare retry for "${testLabel}": ${e}`);
        } finally {
            retryContext.clearRetryFailure(test);
        }
    }

    async uploadTestCache() {
        try {
            await this.cacheUploader.uploadCache();
        } catch (e) {
            logWarning(this.osnTestName, e);
        }
    };

    setStreamKey(value: string) {
        const service = osn.ServiceFactory.legacySettings;
        service.update({ key: value });
        osn.ServiceFactory.legacySettings = service;
        this.setSetting(EOBSSettingsCategories.Stream, 'key', value);
    }

    getStreamKey(): string {
        const service = osn.ServiceFactory.legacySettings;
        return service.settings.key;
    }

    setSetting(category: string, parameter: string, value: any) {
        let oldValue: any;

        // Getting settings container
        const settings = osn.NodeObs.OBS_settings_getSettings(category).data;

        settings.forEach(subCategory => {
            subCategory.parameters.forEach(param => {
                if (param.name === parameter) {
                    oldValue = param.currentValue;
                    param.currentValue = value;
                }
            });
        });

        // Saving updated settings container
        if (value !== oldValue) {
            osn.NodeObs.OBS_settings_saveSettings(category, settings);
        }
    }

    getSetting(category: string, parameter: string): any {
        let value: any;

        // Getting settings container
        const settings = osn.NodeObs.OBS_settings_getSettings(category).data;

        // Getting parameter value
        settings.forEach(subCategory => {
            subCategory.parameters.forEach(param => {
                if (param.name === parameter) {
                    value = param.currentValue;
                }
            });
        });

        return value;
    }

    setSettingsContainer(category: string, settings: any) {
        osn.NodeObs.OBS_settings_saveSettings(category, settings);
    }

    getSettingsContainer(category: string): any {
        return osn.NodeObs.OBS_settings_getSettings(category).data;
    }

    connectOutputSignals() {
        osn.NodeObs.OBS_service_connectOutputSignals((signalInfo: IOBSOutputSignalInfo) => {
            this.signals.push(signalInfo);
        });
    }

    private isTerminalErrorSignal(signalInfo: IOBSOutputSignalInfo, output: string): boolean {
        return signalInfo.type === output
            && ((signalInfo.signal === EOBSOutputSignal.Stop && signalInfo.code !== osn.EOutputCode.Success)
                || signalInfo.signal === EOBSOutputSignal.WriteError);
    }

    private formatSignalInfo(signalInfo: IOBSOutputSignalInfo): string {
        return `${signalInfo.type}/${signalInfo.signal} (code=${signalInfo.code}, error=${signalInfo.error || 'none'})`;
    }

    async getNextSignalInfo(output: string, signal: string): Promise<IOBSOutputSignalInfo> {
        return await this.getNextSignalInfoOf(output, [signal]);
    }

    async getNextSignalInfoOf(output: string, signalsList: string[]): Promise<IOBSOutputSignalInfo> {
        const signalDescription = signalsList.join('/');
        const timeoutMessage = output.replace(/^\w/, c => c.toUpperCase()) + ' ' + signalDescription + ' signal timeout';
        const expectedDeadline = Date.now() + 30000;
        const deadline = Date.now() + 60000; // 60 second timeout for receiving expected signal, since some steps (like recording stop) can take a while on slower CI machines
        const startTime = Date.now();

        while (Date.now() < deadline) {
            const remainingMs = deadline - Date.now();
            const signalInfo = await Promise.race([
                this.signals.shift(),
                new Promise<IOBSOutputSignalInfo>((resolve, reject) => {
                    setTimeout(() => reject(new Error(timeoutMessage)), remainingMs);
                }),
            ]);

            if (signalInfo.type === output && signalsList.indexOf(signalInfo.signal) >= 0) {
                if (Date.now() > expectedDeadline) {
                    logWarning(this.osnTestName, `Received expected ${output}/${signalDescription} signal after ${Date.now() - startTime}ms, which is longer than the expected ${expectedDeadline - startTime}ms. Signal info: ${this.formatSignalInfo(signalInfo)}`);
                }
                return signalInfo;
            }

            if (this.isTerminalErrorSignal(signalInfo, output)) {
                return signalInfo;
            }

            logWarning(
                this.osnTestName,
                `Skipping unexpected output signal while waiting for ${output}/${signalDescription}: ${this.formatSignalInfo(signalInfo)}`,
            );
        }

        throw new Error(timeoutMessage);
    }

    startAutoconfig() {
        osn.NodeObs.InitializeAutoConfig((progressInfo: IConfigProgress) => {
            if (progressInfo.event === 'stopping_step' || progressInfo.event === 'done' || progressInfo.event === 'error') {
                this.progress.push(progressInfo);
            }
        },
            {
                service_name: 'Twitch',
            });
    }

    getNextProgressInfo(autoconfigStep: string): Promise<IConfigProgress> {
        return new Promise((resolve, reject) => {
            this.progress.shift().then(
                function (progressInfo) {
                    resolve(progressInfo)
                }
            );
            setTimeout(() => reject(new Error(autoconfigStep + ' step timeout')), 50000);
        });
    }

    createDefaultVideoContext() {
        logInfo(this.osnTestName, 'createDefaultVideoContext called');
        this.defaultVideoContext = osn.VideoFactory.create();
        const defaultVideoInfo: osn.IVideoInfo = {
            fpsNum: 30,
            fpsDen: 1,
            baseWidth: 1280,
            baseHeight: 720,
            outputWidth: 1280,
            outputHeight: 720,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Bilinear,
            fpsType: osn.EFPSType.Fractional
        };
        this.defaultVideoContext.video = defaultVideoInfo;
    }

    destroyDefaultVideoContext() {
        this.defaultVideoContext.destroy();
        this.defaultVideoContext = null;
    }

    skipSource(inputType: string) {
        if (process.platform === 'darwin') {
            if (inputType === 'browser_source' ||
                inputType === 'window_capture' ||
                inputType === 'monitor_capture' ||
                inputType === 'display_capture' ||
                inputType === 'screen_capture' ||
                inputType === 'coreaudio_input_capture' ||
                inputType === 'coreaudio_output_capture') {
                return true;
            }
        }
        return false
    }

    setSourceMessageListener() {
        osn.NodeObs.RegisterSourceCallback((message: unknown) => {
            console.log('Source callback received' + JSON.stringify(message));
        });

        osn.NodeObs.RegisterSourceMessageCallback((message: unknown) => {
            console.log('Source message callback received' + JSON.stringify(message));
        });
    }

    removeSourceMessageListener() {
        osn.NodeObs.RemoveSourceCallback();
        osn.NodeObs.RemoveSourceMessageCallback();
    }

    /*
    * Is running on darwin CI build agent, which has shown to be particularly flaky with streaming/recording outputs, so 
    * we may want to skip some tests or add extra retries when running in this environment
    */
    isOnDarwinIntelCI()
    {
        // Wrapped this in a function- just incase we want to add more conditions later or disable only within the build agent.
        return this.os === 'darwin' && this.ci && process.arch === 'x64';
    }

    // is the build server environment
    isCI()
    {
        return this.ci;
    }
}
