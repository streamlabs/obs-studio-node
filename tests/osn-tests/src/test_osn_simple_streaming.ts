import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSInputTypes, EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';

const testName = 'osn-simple-streaming';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    // Initialize OBS process
    before(async() => {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);

        obs.instantiateUserPool(testName);

        // Reserving user from pool
        await obs.reserveUser();
    });

    // Shutdown OBS process
    after(async function() {
        // Releasing user got from pool
        await obs.releaseUser();

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

    afterEach(function() {
        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    it('Create simple streaming', async () => {
        const stream = osn.SimpleStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the simple streaming output");

        expect(stream.enforceServiceBitrate).to.equal(
            true, "Invalid enforceServiceBitrate default value");
        expect(stream.enableTwitchVOD).to.equal(
            false, "Invalid enableTwitchVOD default value");
        expect(stream.useAdvanced).to.equal(
            false, "Invalid useAdvanced default value");
        expect(stream.customEncSettings).to.equal(
            '', "Invalid customEncSettings default value");

        stream.enforceServiceBitrate = false;
        stream.enableTwitchVOD = true;
        stream.useAdvanced = true;
        stream.customEncSettings = 'test';
        stream.video = obs.defaultVideoContext;

        expect(stream.enforceServiceBitrate).to.equal(
            false, "Invalid enforceServiceBitrate value");
        expect(stream.enableTwitchVOD).to.equal(
            true, "Invalid enableTwitchVOD value");
        expect(stream.useAdvanced).to.equal(
            true, "Invalid useAdvanced value");
        expect(stream.customEncSettings).to.equal(
            'test', "Invalid customEncSettings value");

        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Start streaming', async () => {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.delay =
            osn.DelayFactory.create();
        stream.reconnect =
            osn.ReconnectFactory.create();
        stream.network =
            osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create();
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        stream.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Streaming, EOBSOutputSignal.Starting);
        expect(signalInfo.type).to.equal(
            EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Starting, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Streaming, EOBSOutputSignal.Activate);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.StreamOutputDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Activate, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Start);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.StreamOutput));

        await sleep(500);

        stream.stop();

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Streaming, EOBSOutputSignal.Stopping);

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stop);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.StreamOutputStoppedWithError,
                signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate);
        expect(signalInfo.type).to.equal(
            EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Deactivate, GetErrorMessage(ETestErrorMsg.StreamOutput));

        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Stream with invalid stream key', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.service.update({ key: 'invalid' });
        stream.delay =
            osn.DelayFactory.create();
        stream.reconnect =
            osn.ReconnectFactory.create();
        stream.network =
            osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create();
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        stream.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Streaming, EOBSOutputSignal.Starting);
        expect(signalInfo.type).to.equal(
            EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Starting, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Streaming, EOBSOutputSignal.Stop);
        expect(signalInfo.type).to.equal(
            EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.code).to.equal(-3, GetErrorMessage(ETestErrorMsg.StreamOutput));

        stream.service.update({ key: obs.userStreamKey });

        osn.SimpleStreamingFactory.destroy(stream);
    });
});
