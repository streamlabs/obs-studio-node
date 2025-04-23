import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSInputTypes, EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';
import { ERecordingFormat, ERecordingQuality } from '../osn';
import path = require('path');

const testName = 'osn-simple-recording';

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

    it('Create simple recording', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        expect(recording).to.not.equal(
            undefined, "Error while creating the simple recording output");

        expect(recording.path).to.equal(
            '', "Invalid path default value");
        expect(recording.format).to.equal(
            ERecordingFormat.MP4, "Invalid format default value");
        expect(recording.fileFormat).to.equal(
            '%CCYY-%MM-%DD %hh-%mm-%ss', "Invalid fileFormat default value");
        expect(recording.overwrite).to.equal(
            false, "Invalid overwrite default value");
        expect(recording.noSpace).to.equal(
            false, "Invalid noSpace default value");
        expect(recording.muxerSettings).to.equal(
            '', "Invalid muxerSettings default value");
        expect(recording.quality).to.equal(
            osn.ERecordingQuality.Stream, "Invalid quality default value");
        expect(recording.lowCPU).to.equal(
            false, "Invalid lowCPU default value");
        
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MOV;
        recording.quality = ERecordingQuality.HighQuality;
        recording.video = obs.defaultVideoContext;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording.lowCPU = true;
        recording.audioEncoder = osn.AudioEncoderFactory.create();
        recording.overwrite = true;
        recording.noSpace = false;

        expect(recording.path).to.equal(
            path.join(path.normalize(__dirname), '..', 'osnData'), "Invalid path value");
        expect(recording.format).to.equal(
            ERecordingFormat.MOV, "Invalid format value");
        expect(recording.quality).to.equal(
            osn.ERecordingQuality.HighQuality, "Invalid quality value");
        expect(recording.lowCPU).to.equal(
            true, "Invalid lowCPU value");
        expect(recording.overwrite).to.equal(
            true, "Invalid overwrite value");
        expect(recording.noSpace).to.equal(
            false, "Invalid noSpace value");

        osn.SimpleRecordingFactory.destroy(recording);
    });

    it('Start simple recording - Stream', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.quality = ERecordingQuality.Stream;
        recording.lowCPU = false;
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        const stream = osn.SimpleStreamingFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.audioEncoder = osn.AudioEncoderFactory.create();
        stream.signalHandler = (signal) => {obs.signals.push(signal)};
        recording.streaming = stream;

        recording.start();

        let signalInfo = await obs.getNextSignalInfo(
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

        osn.SimpleRecordingFactory.destroy(recording);
        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Start simple recording - HighQuality', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.quality = ERecordingQuality.HighQuality;
        recording.video = obs.defaultVideoContext;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording.lowCPU = false;
        recording.audioEncoder = osn.AudioEncoderFactory.create();
        recording.overwrite = false;
        recording.noSpace = false;
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        recording.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Start);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.RecordOutputDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.RecordingOutput));

        await sleep(500);

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

        osn.SimpleRecordingFactory.destroy(recording);
    });

    it('Start simple recording - HigherQuality', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.quality = ERecordingQuality.HigherQuality;
        recording.video = obs.defaultVideoContext;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording.lowCPU = false;
        recording.audioEncoder = osn.AudioEncoderFactory.create();
        recording.overwrite = false;
        recording.noSpace = false;
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        recording.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Start);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.RecordOutputDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.RecordingOutput));

        await sleep(500);

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

        osn.SimpleRecordingFactory.destroy(recording);
    });

    it('Start simple recording - Lossless', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.quality = ERecordingQuality.Lossless;
        recording.video = obs.defaultVideoContext;
        recording.lowCPU = false;
        recording.overwrite = false;
        recording.noSpace = false;
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        recording.start();

        let signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.Recording, EOBSOutputSignal.Start);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.RecordOutputDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(
            EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
        expect(signalInfo.signal).to.equal(
            EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.RecordingOutput));

        await sleep(500);

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

        osn.SimpleRecordingFactory.destroy(recording);
    });
});
