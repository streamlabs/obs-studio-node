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

const testName = 'osn-enhanced-broadcasting-simple-streaming';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    const mediaPath = path.join(path.normalize(__dirname), '..', 'media');
    let secondContext: osn.IVideo = null;

    // Initialize OBS process
    before(async() => {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);

        obs.instantiateUserPool(testName);

        // Reserving user from pool
        await obs.reserveUser();

        secondContext = osn.VideoFactory.create();
        const secondVideoInfo: osn.IVideoInfo = {
            fpsNum: 60,
            fpsDen: 2,
            baseWidth: 720,
            baseHeight: 1280,
            outputWidth: 720,
            outputHeight: 1280,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Full,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: osn.EFPSType.Fractional
        };
        secondContext.video = secondVideoInfo;
    });

    // Shutdown OBS process
    after(async function() {
        // Releasing user got from pool
        await obs.releaseUser();

        if (secondContext) secondContext.destroy();
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

    it('Can be used as the parent stream for simple recording', function() {
        const stream = osn.EnhancedBroadcastingSimpleStreamingFactory.create();
        const recording = osn.SimpleRecordingFactory.create();

        try {
            recording.streaming = stream;

            expect(recording.streaming).to.equal(stream);
        } finally {
            osn.SimpleRecordingFactory.destroy(recording);
            osn.EnhancedBroadcastingSimpleStreamingFactory.destroy(stream);
        }
    });

    it('Can be used as the parent stream for simple replay buffer', function() {
        const stream = osn.EnhancedBroadcastingSimpleStreamingFactory.create();
        const replayBuffer = osn.SimpleReplayBufferFactory.create();

        try {
            replayBuffer.streaming = stream;

            expect(replayBuffer.streaming).to.equal(stream);
        } finally {
            osn.SimpleReplayBufferFactory.destroy(replayBuffer);
            osn.EnhancedBroadcastingSimpleStreamingFactory.destroy(stream);
        }
    });

    it('Enhanced Broadcasting Simple Streaming honors stream delay', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        if (obs.isCI()) {
            // Skipping this test because CI server doesn't have GPU, but you can run it locally
            this.skip();
        }

        const configuredDelayMs = 10 * 1000;
        const allowedTimingDriftMs = 1 * 1000;
        const stream = osn.EnhancedBroadcastingSimpleStreamingFactory.create();
        let streamStartRequested = false;
        let scene: osn.IScene = null;
        let sceneItem: osn.ISceneItem = null;
        let videoSource: osn.IInput = null;

        try {
            expect(stream).to.not.be.null;
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

            scene = osn.SceneFactory.create('stream_delay_scene');
            expect(scene).to.not.be.null;
            osn.Global.setOutputSource(0, scene);

            let settings = inputSettings.ffmpegSource;
            settings['volume'] = 100;
            settings['local_file'] = path.join(mediaPath, "bigbuckbunny.mp4");
            settings['looping'] = true;
            videoSource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, 'stream_delay_video_source', settings);
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
                `Enhanced Broadcasting stream started after ${elapsedMs}ms, expected approximately ${configuredDelayMs}ms delay`,
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
            osn.EnhancedBroadcastingSimpleStreamingFactory.destroy(stream);
        }
    });

    it('Enhanced Broadcasting Simple Streaming Single Canvas', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        if (obs.isCI()) {
            // Skipping this test because CI server doesn't have GPU, but you can run it locally
            this.skip();
        }

        const stream = osn.EnhancedBroadcastingSimpleStreamingFactory.create();
        // Note: no video encoder set, because it is automatically created by the enhanced broadcasting
        stream.service = osn.ServiceFactory.legacySettings;
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-1");
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        stream.start();

        let signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Starting);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Starting, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Activate);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.StreamOutputDidNotStart, signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Activate, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Start);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.StreamOutput));

        // Scene setup
        const scene = osn.SceneFactory.create('my_scene');
        expect(scene).to.not.be.null;
        osn.Global.setOutputSource(0, scene);

        let settings = inputSettings.ffmpegSource;
        settings['volume'] = 100;
        settings['local_file'] = path.join(mediaPath, "bigbuckbunny.mp4");
        settings['looping'] = true;
        const videoSource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, 'video_source', settings);
        expect(videoSource).to.not.be.null;

        const sceneItem = scene.add(videoSource);
        expect(sceneItem).to.not.be.null;
        sceneItem.video = obs.defaultVideoContext;
        sceneItem.visible = true;
        sceneItem.position = { x: 0, y: 0 };

        await sleep(1 * 1000);

        expect(stream.droppedFrames).to.not.equal(undefined, "Undefined droppedFrames");
        expect(stream.totalFrames).to.not.equal(undefined, "Undefined totalFrames");
        expect(stream.kbitsPerSec).to.not.equal(undefined, "Undefined kbitsPerSec");
        expect(stream.dataOutput).to.not.equal(undefined, "Undefined dataOutput");

        stream.stop();

        // Scene cleanup
        osn.Global.setOutputSource(0, null);
        sceneItem.remove();
        videoSource.release();
        scene.release();

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping);

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stop);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.StreamOutputStoppedWithError,
                signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Deactivate, GetErrorMessage(ETestErrorMsg.StreamOutput));

        osn.EnhancedBroadcastingSimpleStreamingFactory.destroy(stream);
    });

    it('Enhanced Broadcasting Simple Streaming Dual Canvas', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        if (obs.isCI()) {
            // Skipping this test because CI server doesn't have GPU, but you can run it locally
            this.skip();
        }

        const stream = osn.EnhancedBroadcastingSimpleStreamingFactory.create();
        expect(stream).to.not.be.null;
        stream.service = osn.ServiceFactory.legacySettings;
        // Note: no video encoder set, because it is automatically created by the enhanced broadcasting
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.additionalVideo = secondContext;
        stream.audioEncoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-simple-streaming-2");
        stream.signalHandler = (signal) => {obs.signals.push(signal)};

        stream.start();

        let signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Starting);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Starting, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Activate);

        if (signalInfo.signal == EOBSOutputSignal.Stop) {
            throw Error(GetErrorMessage(ETestErrorMsg.StreamOutputDidNotStart, signalInfo.code.toString()));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Activate, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Start);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Start, GetErrorMessage(ETestErrorMsg.StreamOutput));

        // Scene setup
        const scene = osn.SceneFactory.create('my_scene');
        expect(scene).to.not.be.null;
        osn.Global.setOutputSource(0, scene);

        let settings = inputSettings.ffmpegSource;
        settings['volume'] = 100;
        settings['local_file'] = path.join(mediaPath, "bigbuckbunny.mp4");
        settings['looping'] = true;
        const videoSource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, 'video_source', settings);
        expect(videoSource).to.not.be.null;

        // Item on the horizontal canvas
        const sceneItem1 = scene.add(videoSource);
        expect(sceneItem1).to.not.be.null;
        sceneItem1.video = obs.defaultVideoContext;
        sceneItem1.visible = true;
        sceneItem1.position = { x: 0, y: 0 };

        // Item on the vertical canvas
        const sceneItem2 = scene.add(videoSource);
        expect(sceneItem2).to.not.be.null;
        sceneItem2.video = secondContext;
        sceneItem2.visible = true;
        sceneItem2.position = { x: 20, y: 20 };
        sceneItem2.scale = {x: 0.5, y: 0.5};

        await sleep(1 * 1000);

        expect(stream.droppedFrames).to.not.equal(undefined, "Undefined droppedFrames");
        expect(stream.totalFrames).to.not.equal(undefined, "Undefined totalFrames");
        expect(stream.kbitsPerSec).to.not.equal(undefined, "Undefined kbitsPerSec");
        expect(stream.dataOutput).to.not.equal(undefined, "Undefined dataOutput");

        stream.stop();

        // Scene cleanup
        osn.Global.setOutputSource(0, null);
        sceneItem1.remove();
        sceneItem2.remove();
        videoSource.release();
        scene.release();

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping);

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stopping, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stop);

        if (signalInfo.code != 0) {
            throw Error(GetErrorMessage(
                ETestErrorMsg.StreamOutputStoppedWithError,
                signalInfo.code.toString(), signalInfo.error));
        }

        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Stop, GetErrorMessage(ETestErrorMsg.StreamOutput));

        signalInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate);
        expect(signalInfo.type).to.equal(EOBSOutputType.Streaming, GetErrorMessage(ETestErrorMsg.StreamOutput));
        expect(signalInfo.signal).to.equal(EOBSOutputSignal.Deactivate, GetErrorMessage(ETestErrorMsg.StreamOutput));

        osn.EnhancedBroadcastingSimpleStreamingFactory.destroy(stream);
    });
});
