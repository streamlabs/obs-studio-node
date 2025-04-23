import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSInputTypes, EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';
import path = require('path');

const testName = 'osn-simple-replay-buffer';

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

    it('Create simple replay buffer', async () => {
        const replayBuffer = osn.SimpleReplayBufferFactory.create();
        expect(replayBuffer).to.not.equal(
            undefined, "Error while creating the simple replayBuffer output");

        expect(replayBuffer.path).to.equal(
            '', "Invalid path default value");
        expect(replayBuffer.format).to.equal(
            osn.ERecordingFormat.MP4, "Invalid format default value");
        expect(replayBuffer.fileFormat).to.equal(
            '%CCYY-%MM-%DD %hh-%mm-%ss', "Invalid fileFormat default value");
        expect(replayBuffer.overwrite).to.equal(
            false, "Invalid overwrite default value");
        expect(replayBuffer.noSpace).to.equal(
            false, "Invalid noSpace default value");
        expect(replayBuffer.muxerSettings).to.equal(
            '', "Invalid muxerSettings default value");
        expect(replayBuffer.duration).to.equal(
            20, "Invalid duration default value");
        expect(replayBuffer.prefix).to.equal(
            'Replay', "Invalid prefix default value");
        expect(replayBuffer.suffix).to.equal(
            '', "Invalid suffix default value");
        expect(replayBuffer.usesStream).to.equal(
            false, "Invalid usesStream default value");

        replayBuffer.path = path.join(path.normalize(__dirname), '..', 'osnData');
        replayBuffer.format = osn.ERecordingFormat.MOV;
        replayBuffer.overwrite = true;
        replayBuffer.noSpace = false;
        replayBuffer.video = obs.defaultVideoContext;
        replayBuffer.duration = 60;
        replayBuffer.prefix = 'Prefix';
        replayBuffer.suffix = 'Suffix';
        replayBuffer.usesStream = true;

        expect(replayBuffer.path).to.equal(
            path.join(path.normalize(__dirname), '..', 'osnData'), "Invalid path value");
        expect(replayBuffer.format).to.equal(
            osn.ERecordingFormat.MOV, "Invalid format value");
        expect(replayBuffer.overwrite).to.equal(
            true, "Invalid overwrite value");
        expect(replayBuffer.noSpace).to.equal(
            false, "Invalid noSpace value");
        expect(replayBuffer.duration).to.equal(
            60, "Invalid duration value");
        expect(replayBuffer.prefix).to.equal(
            'Prefix', "Invalid prefix value");
        expect(replayBuffer.suffix).to.equal(
            'Suffix', "Invalid suffix value");
        expect(replayBuffer.usesStream).to.equal(
            true, "Invalid usesStream value");

        osn.SimpleReplayBufferFactory.destroy(replayBuffer);
    });

    it('Start simple replay buffer - Use Recording', async () => {
        const replayBuffer = osn.SimpleReplayBufferFactory.create();
        replayBuffer.path = path.join(path.normalize(__dirname), '..', 'osnData');
        replayBuffer.format = osn.ERecordingFormat.MP4;
        replayBuffer.overwrite = false;
        replayBuffer.noSpace = false;
        replayBuffer.video = obs.defaultVideoContext;
        replayBuffer.signalHandler = (signal) => {obs.signals.push(signal)};
        replayBuffer.duration = 60;
        replayBuffer.prefix = 'Prefix';
        replayBuffer.suffix = 'Suffix';

        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = osn.ERecordingFormat.MP4;
        recording.quality = osn.ERecordingQuality.HighQuality;
        recording.video = obs.defaultVideoContext;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording.audioEncoder = osn.AudioEncoderFactory.create();
        recording.lowCPU = false;
        recording.overwrite = false;
        recording.noSpace = false;

        replayBuffer.recording = recording;

        replayBuffer.start();
        recording.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Start);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.ReplayBufferDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        await sleep(500);

        replayBuffer.save();

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Writing);
        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Writing, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Wrote);
        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Wrote, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        replayBuffer.stop();
        recording.stop();

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stopping);
        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stop);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(ETestErrorMsg.ReplayBufferStoppedWithError, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        const lastFile = replayBuffer.lastFile().split('/');
        const expectedPrefix = lastFile[lastFile.length - 1].startsWith('Prefix');
        const expectedSuffix = lastFile[lastFile.length - 1].endsWith('Suffix.mp4');

        expect(expectedPrefix).to.equal(true, 'Wrong prefix when saving the simple replay buffer');
        expect(expectedSuffix).to.equal(true, 'Wrong suffix when saving the simple replay buffer');

        osn.SimpleReplayBufferFactory.destroy(replayBuffer);
        osn.SimpleRecordingFactory.destroy(recording);
    });

    it('Start simple replay buffer - Use Stream through Recording', async () => {
        const replayBuffer = osn.SimpleReplayBufferFactory.create();
        replayBuffer.path = path.join(path.normalize(__dirname), '..', 'osnData');
        replayBuffer.format = osn.ERecordingFormat.MP4;
        replayBuffer.overwrite = false;
        replayBuffer.noSpace = false;
        replayBuffer.video = obs.defaultVideoContext;
        replayBuffer.signalHandler = (signal) => {obs.signals.push(signal)};
        replayBuffer.duration = 60;
        replayBuffer.prefix = 'Prefix';
        replayBuffer.suffix = 'Suffix';

        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = osn.ERecordingFormat.MP4;
        recording.quality = osn.ERecordingQuality.Stream;
        recording.video = obs.defaultVideoContext;
        recording.lowCPU = false;
        recording.overwrite = false;
        recording.noSpace = false;
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        const stream = osn.SimpleStreamingFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.audioEncoder = osn.AudioEncoderFactory.create();
        stream.signalHandler = (signal) => {obs.signals.push(signal)};
        recording.streaming = stream;

        replayBuffer.recording = recording;

        replayBuffer.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Start);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.ReplayBufferDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        recording.start();

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Start);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.RecordOutputDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.RecordingOutput));

        stream.start();

        signalInfo = await obs.getNextSignalInfo(
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

        replayBuffer.save();

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Writing);
        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Writing, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Wrote);
        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Wrote, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        replayBuffer.stop();

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stopping);
        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stop);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(ETestErrorMsg.ReplayBufferStoppedWithError, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.ReplayBuffer, GetErrorMessage(ETestErrorMsg.ReplayBuffer));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.ReplayBuffer));

        recording.stop();
        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Stopping);
        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.RecordingOutput));

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Stop);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.RecordOutputStoppedWithError, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.RecordingOutput));

        signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Wrote);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.RecordOutputStoppedWithError, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Wrote, GetErrorMessage(ETestErrorMsg.RecordingOutput));

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

        const lastFile = replayBuffer.lastFile().split('/');
        const expectedPrefix = lastFile[lastFile.length - 1].startsWith('Prefix');
        const expectedSuffix = lastFile[lastFile.length - 1].endsWith('Suffix.mp4');

        expect(expectedPrefix).to.equal(true, 'Wrong prefix when saving the simple replay buffer');
        expect(expectedSuffix).to.equal(true, 'Wrong suffix when saving the simple replay buffer');

        osn.SimpleReplayBufferFactory.destroy(replayBuffer);
        osn.SimpleRecordingFactory.destroy(recording);
        osn.SimpleStreamingFactory.destroy(stream);
    });
});
