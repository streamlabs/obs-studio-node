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

const testName = 'osn-enhanced-broadcasting-advanced-streaming';

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

        secondContext.destroy();
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

    // TODO: more tests:
    // - vertical primary canvas

    it('Enhanced Broadcasting Advanced Streaming rejects without crashing in CI', function() {
        // This test is CI only because CI is expected to hit a Twitch Enhanced Broadcasting rejection.
        if (obs.isDarwin()) {
            this.skip();
        }

        if (!obs.isCI()) {
            this.skip();
        }

        const stream = osn.EnhancedBroadcastingAdvancedStreamingFactory.create();
        let startError: Error = null;

        try {
            expect(stream).to.not.be.null;
            stream.service = osn.ServiceFactory.legacySettings;
            stream.service.update({
                service: 'Twitch',
                server: 'auto',
                key: obs.userStreamKey,
            });
            stream.delay = osn.DelayFactory.create();
            stream.reconnect = osn.ReconnectFactory.create();
            stream.network = osn.NetworkFactory.create();
            stream.video = obs.defaultVideoContext;
            const track1 = osn.AudioTrackFactory.create(160, 'track1');
            osn.AudioTrackFactory.setAtIndex(track1, 1);

            try {
                stream.start();
            } catch (error) {
                startError = error as Error;
            }

            expect(startError).to.not.be.null;
            expect(osn.ServiceFactory.types()).to.include('rtmp_common');
        } finally {
            if (!startError) {
                try {
                    stream.stop();
                } catch (error) {
                    // Best-effort cleanup if the stream unexpectedly started.
                }
            }

            osn.EnhancedBroadcastingAdvancedStreamingFactory.destroy(stream);
        }
    });

    it('Enhanced Broadcasting Advanced Streaming Single Canvas', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        if (obs.isCI()) {
            // Skipping this test because CI server doesn't have GPU, but you can run it locally
            this.skip();
        }

        const stream = osn.EnhancedBroadcastingAdvancedStreamingFactory.create();
        expect(stream).to.not.be.null;
        stream.service = osn.ServiceFactory.legacySettings;
        // Note: no video encoder set, because it is automatically created by the enhanced broadcasting
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
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

        osn.EnhancedBroadcastingAdvancedStreamingFactory.destroy(stream);
    });

    it('Enhanced Broadcasting Advanced Streaming Dual Canvas', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        if (obs.isCI()) {
            // Skipping this test because CI server doesn't have GPU, but you can run it locally
            this.skip();
        }

        const stream = osn.EnhancedBroadcastingAdvancedStreamingFactory.create();
        expect(stream).to.not.be.null;
        stream.service = osn.ServiceFactory.legacySettings;
        // Note: no video encoder set, because it is automatically created by the enhanced broadcasting
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.additionalVideo = secondContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
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

        osn.EnhancedBroadcastingAdvancedStreamingFactory.destroy(stream);
    });
});

