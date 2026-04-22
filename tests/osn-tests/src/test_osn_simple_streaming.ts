import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSInputTypes, EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';
import * as inputSettings from '../util/input_settings';

import path = require('path');

const testName = 'osn-simple-streaming';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    const mediaPath = path.join(path.normalize(__dirname), '..', 'media');

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

    it('Stream with missing video encoder', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.service = osn.ServiceFactory.legacySettings;
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-1");
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        expect(() => {
            stream.start();
        }).throw('Invalid video encoder');


        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Stream with missing audio encoder', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.video = obs.defaultVideoContext;
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        expect(() => {
            stream.start();
        }).throw('Invalid audio encoder');


        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Stream with missing service', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-2");
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        expect(() => {
            stream.start();
        }).throw('Invalid service');


        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Stream with missing canvas', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-3");
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        expect(() => {
            stream.start();
        }).throw('Invalid main canvas');

        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Start streaming', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-simple-streaming-1');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.delay =
            osn.DelayFactory.create();
        stream.reconnect =
            osn.ReconnectFactory.create();
        stream.network =
            osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-4");
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

        expect(stream.droppedFrames).to.not.equal(
            undefined, "Undefined droppedFrames");
        expect(stream.totalFrames).to.not.equal(
            undefined, "Undefined totalFrames");
        expect(stream.kbitsPerSec).to.not.equal(
            undefined, "Undefined kbitsPerSec");
        expect(stream.dataOutput).to.not.equal(
            undefined, "Undefined dataOutput");

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

        const streamEncoder = stream.videoEncoder;
        const audioEncoder = stream.audioEncoder;
        osn.SimpleStreamingFactory.destroy(stream);
        streamEncoder.release();
        audioEncoder.release();
    });

    it('Simple Streaming honors stream delay', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        const configuredDelayMs = 10 * 1000;
        const allowedTimingDriftMs = 1 * 1000;
        const stream = osn.SimpleStreamingFactory.create();
        let streamStartRequested = false;
        let scene: osn.IScene = null;
        let sceneItem: osn.ISceneItem = null;
        let videoSource: osn.IInput = null;

        try {
            expect(stream).to.not.be.null;
            stream.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-simple-streaming-delay');
            stream.service = osn.ServiceFactory.legacySettings;
            stream.delay = osn.DelayFactory.create();
            stream.delay.enabled = true;
            stream.delay.delaySec = configuredDelayMs / 1000;
            stream.delay.preserveDelay = true;
            stream.reconnect = osn.ReconnectFactory.create();
            stream.network = osn.NetworkFactory.create();
            stream.video = obs.defaultVideoContext;
            stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-delay");
            stream.signalHandler = (signal) => {obs.signals.push(signal)};

            scene = osn.SceneFactory.create('simple_stream_delay_scene');
            expect(scene).to.not.be.null;
            osn.Global.setOutputSource(0, scene);

            let settings = inputSettings.ffmpegSource;
            settings['volume'] = 100;
            settings['local_file'] = path.join(mediaPath, "bigbuckbunny.mp4");
            settings['looping'] = true;
            videoSource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, 'simple_stream_delay_video_source', settings);
            expect(videoSource).to.not.be.null;

            sceneItem = scene.add(videoSource);
            expect(sceneItem).to.not.be.null;
            sceneItem.video = obs.defaultVideoContext;
            sceneItem.visible = true;
            sceneItem.position = { x: 0, y: 0 };

            const startTimeMs = Date.now();
            stream.start();
            streamStartRequested = true;

            const preStartSignals = [EOBSOutputSignal.Starting, EOBSOutputSignal.Activate];
            while (preStartSignals.length > 0) {
                let signalInfo = await obs.getNextSignalInfoOf(EOBSOutputType.Streaming, preStartSignals);
                const signalIndex = preStartSignals.indexOf(signalInfo.signal);
                expect(signalIndex).to.be.greaterThan(-1, GetErrorMessage(ETestErrorMsg.StreamOutput));
                expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
                preStartSignals.splice(signalIndex, 1);
            }

            let signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Start);
            expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
            expect(signalInfo.signal).to.equal(EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.StreamOutput));

            const elapsedMs = Date.now() - startTimeMs;
            expect(elapsedMs).to.be.greaterThan(
                configuredDelayMs - allowedTimingDriftMs,
                `Simple stream started after ${elapsedMs}ms, expected approximately ${configuredDelayMs}ms delay`,
            );
        } finally {
            if (streamStartRequested) {
                try {
                    stream.stop();
                    await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping);
                    await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stop);
                    await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate);
                } catch (e) {
                    // Preserve the original assertion failure if cleanup also fails.
                }
            }

            osn.Global.setOutputSource(0, null);
            if (sceneItem) sceneItem.remove();
            if (videoSource) videoSource.release();
            if (scene) scene.release();

            const streamEncoder = stream.videoEncoder;
            const audioEncoder = stream.audioEncoder;
            osn.SimpleStreamingFactory.destroy(stream);
            if (streamEncoder) streamEncoder.release();
            if (audioEncoder) audioEncoder.release();
        }
    });

    it('Stream with invalid stream key', async function() {
        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-simple-streaming-2');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.service.update({ key: 'invalid' });
        stream.delay =
            osn.DelayFactory.create();
        stream.reconnect =
            osn.ReconnectFactory.create();
        stream.network =
            osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-5");
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        const videoEncoder = stream.videoEncoder;
        const audioEncoder = stream.audioEncoder;
        try {
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
        } finally {
            stream.service.update({ key: obs.userStreamKey });
            osn.SimpleStreamingFactory.destroy(stream);
            videoEncoder.release();
            audioEncoder.release();
        }
    });
});
