import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine, logWarning } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler, IConfigProgress } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';

const testName = 'nodeobs_autoconfig';

describe(testName, function() {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    // Initialize OBS process
    before(async function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName, false);

        obs.instantiateUserPool(testName);

        // Reserving user from pool
        await obs.reserveUser();
    });

    // Shutdown OBS process
    after(async function() {

        // Releasing user got from pool
        await obs.releaseUser();

        // Closing OBS process
        obs.shutdown();

        if (hasTestFailed === true) {
            logInfo(testName, 'One or more test cases failed. Uploading cache');
            await obs.uploadTestCache();
        }

        obs = null;
        deleteConfigFiles();
        logInfo(testName, 'Finished ' + testName + ' tests');
        logEmptyLine();
    });

    afterEach(async function() {
        hasTestFailed = (await obs.finalizeRetryableTest(this)) || hasTestFailed;
    });

    it('Run autoconfig', async function() {
        const start = performance.now();
        let progressInfo: IConfigProgress;
	    let settingValue: any;

        obs.startAutoconfig();

        osn.NodeObs.StartBandwidthTest();

        progressInfo = await obs.getNextProgressInfo('Bandwidth test');

	    if (progressInfo.event != 'error') {
            expect(progressInfo.event).to.equal('stopping_step', GetErrorMessage(ETestErrorMsg.BandwidthTest));
            expect(progressInfo.description).to.equal('bandwidth_test',  GetErrorMessage(ETestErrorMsg.BandwidthTest));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.BandwidthTest));

            osn.NodeObs.StartStreamEncoderTest();

            progressInfo = await obs.getNextProgressInfo('Stream Encoder test');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.StreamEncoderTest));
            expect(progressInfo.description).to.equal('streamingEncoder_test',  GetErrorMessage(ETestErrorMsg.StreamEncoderTest));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.StreamEncoderTest));

            osn.NodeObs.StartRecordingEncoderTest();

            progressInfo = await obs.getNextProgressInfo('Recording Encoder test');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.RecordingEncoderTest));
            expect(progressInfo.description).to.equal('recordingEncoder_test',  GetErrorMessage(ETestErrorMsg.RecordingEncoderTest));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.RecordingEncoderTest));

            osn.NodeObs.StartCheckSettings();

            progressInfo = await obs.getNextProgressInfo('Check Settings');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.CheckSettings));
            expect(progressInfo.description).to.equal('checking_settings',  GetErrorMessage(ETestErrorMsg.CheckSettings));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.CheckSettings));
/*
            osn.NodeObs.StartSaveStreamSettings();

            progressInfo = await obs.getNextProgressInfo('Save Stream Settings');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.SaveStreamSettings));
            expect(progressInfo.description).to.equal('saving_service',  GetErrorMessage(ETestErrorMsg.SaveStreamSettings));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.SaveStreamSettings));

            osn.NodeObs.StartSaveSettings();

            progressInfo = await obs.getNextProgressInfo('Save Settings');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));
            expect(progressInfo.description).to.equal('saving_settings',  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));

            progressInfo = await obs.getNextProgressInfo('Autoconfig done');
            expect(progressInfo.event).to.equal('done',  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));
            */
        } else {
            logWarning(testName, 'Bandwidth test failed with ' + progressInfo.description + '. Setting default settings');

            osn.NodeObs.StartSetDefaultSettings();

            progressInfo = await obs.getNextProgressInfo('Set Default Settings');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.SetDefaultSettings));
            expect(progressInfo.description).to.equal('setting_default_settings',  GetErrorMessage(ETestErrorMsg.SetDefaultSettings));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.SetDefaultSettings));

            osn.NodeObs.StartSaveStreamSettings();

            progressInfo = await obs.getNextProgressInfo('Save Stream Settings');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.SaveStreamSettings));
            expect(progressInfo.description).to.equal('saving_service',  GetErrorMessage(ETestErrorMsg.SaveStreamSettings));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.SaveStreamSettings));

            osn.NodeObs.StartSaveSettings();

            progressInfo = await obs.getNextProgressInfo('Save Settings');
            expect(progressInfo.event).to.equal('stopping_step',  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));
            expect(progressInfo.description).to.equal('saving_settings',  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));
            expect(progressInfo.percentage).to.equal(100,  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));

            progressInfo = await obs.getNextProgressInfo('Autoconfig done');
            expect(progressInfo.event).to.equal('done',  GetErrorMessage(ETestErrorMsg.SaveSettingsStep));

            // Checking default settings
            settingValue = obs.getSetting('Output', 'Mode');
            expect(settingValue).to.equal('Simple',  GetErrorMessage(ETestErrorMsg.DefaultOutputMode));

            settingValue = obs.getSetting('Output', 'VBitrate');
            expect(settingValue).to.equal(4500, GetErrorMessage(ETestErrorMsg.DefaultVBitrate));

            settingValue = obs.getSetting('Output', 'StreamEncoder');
            expect(settingValue).to.equal('x264', GetErrorMessage(ETestErrorMsg.DefaultStreamEncoder));

            settingValue = obs.getSetting('Output', 'RecQuality');
            expect(settingValue).to.equal('Small', GetErrorMessage(ETestErrorMsg.DefaultRecQuality));

            settingValue = obs.getSetting('Advanced', 'DynamicBitrate');
            expect(settingValue).to.equal(false, GetErrorMessage(ETestErrorMsg.DefaultDinamicBitrate));

            settingValue = obs.getSetting('Video', 'Output');
            expect(settingValue).to.equal('1280x720', GetErrorMessage(ETestErrorMsg.DefaultVideoOutput));

            settingValue = obs.getSetting('Video', 'FPSType');
            expect(settingValue).to.equal('Common FPS Values', GetErrorMessage(ETestErrorMsg.DefaultFPSType));

            settingValue = obs.getSetting('Video', 'FPSCommon');
            expect(settingValue).to.equal('30', GetErrorMessage(ETestErrorMsg.DefaultFPSCommon));
            const end = performance.now();
            logInfo(testName, `Elapsed time: ${end - start} milliseconds`);
        }

	osn.NodeObs.TerminateAutoConfig();
    });
});
