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
const fs = require('fs');

const testName = 'osn-advanced-recording';
const customFilenamePattern = '%CCYY-%MM-%DD_%hh-%mm-%ss-%s-%%';

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

    afterEach(async function() {
        hasTestFailed = (await obs.finalizeRetryableTest(this)) || hasTestFailed;
    });

    it('Create advanced recording', async () => {
        const recording = osn.AdvancedRecordingFactory.create();
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
        expect(recording.mixer).to.equal(
            1, "Invalid mixer default value");
        expect(recording.rescaling).to.equal(
            false, "Invalid rescaling default value");
        expect(recording.outputWidth).to.equal(
            1280, "Invalid outputWidth default value");
        expect(recording.outputHeight).to.equal(
            720, "Invalid outputHeight default value");
        expect(recording.useStreamEncoders).to.equal(
            true, "Invalid useStreamEncoders default value");
        
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MOV;
        recording.fileFormat = customFilenamePattern;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-adv-recording-1');
        recording.overwrite = true;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        recording.mixer = 7;
        recording.rescaling = true;
        recording.outputWidth = 1920;
        recording.outputHeight = 1080;
        recording.useStreamEncoders = false;

        expect(recording.path).to.equal(
            path.join(path.normalize(__dirname), '..', 'osnData'), "Invalid path value");
        expect(recording.format).to.equal(
            ERecordingFormat.MOV, "Invalid format value");
        expect(recording.fileFormat).to.equal(
            customFilenamePattern, "Invalid fileFormat value");
        expect(recording.overwrite).to.equal(
            true, "Invalid overwrite value");
        expect(recording.noSpace).to.equal(
            false, "Invalid noSpace value");
        expect(recording.mixer).to.equal(
            7, "Invalid mixer default value");
        expect(recording.rescaling).to.equal(
            true, "Invalid rescaling default value");
        expect(recording.outputWidth).to.equal(
            1920, "Invalid outputWidth default value");
        expect(recording.outputHeight).to.equal(
            1080, "Invalid outputHeight default value");
        expect(recording.useStreamEncoders).to.equal(
            false, "Invalid useStreamEncoders default value");

        const videoEncoder = recording.videoEncoder;
        osn.AdvancedRecordingFactory.destroy(recording);
        videoEncoder.release();
    });

    it('Start advanced recording - Stream', async function () {
        if (obs.isDarwin()) {
            this.skip();
        }
        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.fileFormat = customFilenamePattern;
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        recording.signalHandler = (signal) => {obs.signals.push(signal)};
        recording.useStreamEncoders = true;
        const stream = osn.AdvancedStreamingFactory.create();
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-adv-streaming-1');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.video = obs.defaultVideoContext;
        stream.signalHandler = (signal) => {obs.signals.push(signal)};
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
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
                ETestErrorMsg.StreamOutputDidNotStart, signalInfo.code.toString()));
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

        const lastFile = path.basename(recording.lastFile());
        expect(lastFile).to.match(
            /^\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}-\d+-%\.mp4$/,
            'Wrong recording filename formatting',
        );

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

        const videoEncoder = stream.videoEncoder;
        osn.AdvancedRecordingFactory.destroy(recording);
        osn.AdvancedStreamingFactory.destroy(stream);
        videoEncoder.release();
    });

    it('Start advanced recording - Custom encoders', async function () {
        if (obs.isDarwin()) {
            this.skip();
        }
        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-adv-recording-2');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
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

        const videoEncoder = recording.videoEncoder;
        osn.AdvancedRecordingFactory.destroy(recording);
        videoEncoder.release();
    });

    it('Start advanced recording - Enable file split every second', async function () {
        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.enableFileSplit = true;
        recording.splitTime = 1; // Split file every 1 second
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-adv-recording-2');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        try {
            const filesBeforeRecording = fs.readdirSync(recording.path);
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

            await sleep(9000); // Wait for 9 seconds to ensure multiple splits happen

            recording.stop();

            signalInfo = await obs.getNextSignalInfo(
                EOBSOutputType.Recording, EOBSOutputSignal.Stopping);
            expect(signalInfo.type).to.equal(
                EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
            expect(signalInfo.signal).to.equal(
                EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.RecordingOutput));

            const filesAfterRecording = fs.readdirSync(recording.path);
            logInfo(testName, `Number of files generated: ${filesAfterRecording.length - filesBeforeRecording.length}`);
            expect(filesAfterRecording.length).to.be.greaterThan(filesBeforeRecording.length + 2, "File split did not create the expected minimum number of files");
        } finally {
            const videoEncoder = recording.videoEncoder;
            osn.AdvancedRecordingFactory.destroy(recording);
            videoEncoder.release();
        }
    });

    it('Start advanced recording - Enable file split every few bytes', async function () {
        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.enableFileSplit = true;
        recording.splitType = osn.ERecSplitType.Size;
        recording.splitSize = 1; // Split file every byte
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-adv-recording-2');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
        recording.signalHandler = (signal) => {obs.signals.push(signal)};

        try {
            const filesBeforeRecording = fs.readdirSync(recording.path);
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

            await sleep(15000); // Wait for 15 seconds to ensure multiple splits happen

            recording.stop();

            signalInfo = await obs.getNextSignalInfo(
                EOBSOutputType.Recording, EOBSOutputSignal.Stopping);
            expect(signalInfo.type).to.equal(
                EOBSOutputType.Recording, GetErrorMessage(ETestErrorMsg.RecordingOutput));
            expect(signalInfo.signal).to.equal(
                EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.RecordingOutput));

            const filesAfterRecording = fs.readdirSync(recording.path);
            logInfo(testName, `Number of files generated is ${filesAfterRecording.length - filesBeforeRecording.length} after using size-based splitting`);
            expect(filesAfterRecording.length).to.be.greaterThan(filesBeforeRecording.length + 2, "File split did not create the expected minimum number of files");
        } finally {
            const videoEncoder = recording.videoEncoder;
            osn.AdvancedRecordingFactory.destroy(recording);
            videoEncoder.release();
        }
    });
});
