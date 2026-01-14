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

const testName = 'osn-advanced-streaming';

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

    afterEach(function() {
        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    it('Create advanced streaming', async () => {
        const stream = osn.AdvancedStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the simple streaming output");

        expect(stream.enforceServiceBitrate).to.equal(
            true, "Invalid enforceServiceBitrate default value");
        expect(stream.enableTwitchVOD).to.equal(
            false, "Invalid enableTwitchVOD default value");
        expect(stream.audioTrack).to.equal(
            1, "Invalid audioTrack default value");
        expect(stream.twitchTrack).to.equal(
            2, "Invalid twitchTrack default value");
        expect(stream.rescaling).to.equal(
            false, "Invalid rescaling default value");
        expect(stream.outputWidth).to.equal(
            1280, "Invalid outputWidth default value");
        expect(stream.outputHeight).to.equal(
            720, "Invalid outputHeight default value");

        stream.enforceServiceBitrate = false;
        stream.enableTwitchVOD = true;
        stream.audioTrack = 2;
        stream.twitchTrack = 3;
        stream.rescaling = true;
        stream.outputWidth = 1920;
        stream.outputHeight = 1080;
        stream.video = obs.defaultVideoContext;

        expect(stream.enforceServiceBitrate).to.equal(
            false, "Invalid enforceServiceBitrate value");
        expect(stream.enableTwitchVOD).to.equal(
            true, "Invalid enableTwitchVOD value");
        expect(stream.audioTrack).to.equal(
            2, "Invalid audioTrack default value");
        expect(stream.twitchTrack).to.equal(
            3, "Invalid twitchTrack default value");
        expect(stream.rescaling).to.equal(
            true, "Invalid rescaling default value");
        expect(stream.outputWidth).to.equal(
            1920, "Invalid outputWidth default value");
        expect(stream.outputHeight).to.equal(
            1080, "Invalid outputHeight default value");

        osn.AdvancedStreamingFactory.destroy(stream);
    });

    it('Start streaming', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }
        const stream = osn.AdvancedStreamingFactory.create();
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
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);

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
                ETestErrorMsg.StreamOutputDidNotStart, signalInfo.code.toString()));
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

        osn.AdvancedStreamingFactory.destroy(stream);
    });

    it('Enhanced broadcasting streaming', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }

        const stream = osn.AdvancedStreamingFactory.create();
        expect(stream).to.not.be.null;
        stream.service = osn.ServiceFactory.legacySettings;

        // Note: no video encoder set, because it is automatically created by the enhanced broadcasting
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.enhancedBroadcasting = true;
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

        await sleep(5 * 1000);

        expect(stream.droppedFrames).to.not.equal(undefined, "Undefined droppedFrames");
        expect(stream.totalFrames).to.not.equal(undefined, "Undefined totalFrames");
        expect(stream.kbitsPerSec).to.not.equal(undefined, "Undefined kbitsPerSec");
        expect(stream.dataOutput).to.not.equal(undefined, "Undefined dataOutput");

        stream.stop();

        // Scene cleanup
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

        osn.AdvancedStreamingFactory.destroy(stream);
    });

    it('Stream with invalid stream key', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }
        const stream = osn.AdvancedStreamingFactory.create();
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
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
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

        osn.AdvancedStreamingFactory.destroy(stream);
    });
});
