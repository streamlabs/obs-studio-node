import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler, IOBSOutputSignalInfo, IVec2 } from '../util/obs_handler';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { IInput, ISettings, ITimeSpec } from '../osn';
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSInputTypes, EOBSOutputSignal, EOBSOutputType, EOBSSettingsCategories } from '../util/obs_enums';
import { ERecordingFormat, ERecordingQuality } from '../osn';
import { EFPSType } from '../osn';
import { UserPoolHandler } from '../util/user_pool_handler';
import * as inputSettings from '../util/input_settings';

import path = require('path');
import { randomUUID } from 'crypto';

const testName = 'osn-dual-output';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    let newSceneName = 'scene_' + randomUUID();
    let newSourceName: string = 'image_source_' + randomUUID();
    const media_path = path.join(path.normalize(__dirname), '..', 'media');
    let secondContext;

    // Initialize OBS process
    before(async () => {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);

        obs.instantiateUserPool(testName);

        // Reserving user from pool
        await obs.reserveUser();

        // Connecting output signals
        obs.connectOutputSignals();

        secondContext = osn.VideoFactory.create();
        const secondVideoInfo: osn.IVideoInfo = {
            fpsNum: 60,
            fpsDen: 2,
            baseWidth: 1080,
            baseHeight: 1920,
            outputWidth: 1080,
            outputHeight: 1920,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Full,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: EFPSType.Fractional
        };
        secondContext.video = secondVideoInfo;
    });

    // Shutdown OBS process
    after(async function () {
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

    beforeEach(function () {
        // Creating scene
        const scene = osn.SceneFactory.create(newSceneName);

        // Checking if scene was created correctly
        expect(scene).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateScene, newSceneName));
        expect(scene.id).to.equal('scene', GetErrorMessage(ETestErrorMsg.SceneId, newSceneName));
        expect(scene.name).to.equal(newSceneName, GetErrorMessage(ETestErrorMsg.SceneName, newSceneName));
        expect(scene.type).to.equal(osn.ESourceType.Scene, GetErrorMessage(ETestErrorMsg.SceneType, newSceneName));

        // Creating input source
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, newSourceName);

        // Checking if source was created correctly
        expect(source).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, EOBSInputTypes.ImageSource));
        expect(source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.InputId, EOBSInputTypes.ImageSource));
        expect(source.name).to.equal(newSourceName, GetErrorMessage(ETestErrorMsg.InputName, EOBSInputTypes.ImageSource));

        osn.Global.setOutputSource(0, scene);
    });

    afterEach(function () {
        const scene = osn.SceneFactory.fromName(newSceneName);
        scene.release();

        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    async function handleStreamSignals(expectedType: EOBSOutputType, expectedSignal: EOBSOutputSignal, errorMessage: string): Promise<void> {
        const signalInfo = await obs.getNextSignalInfo(expectedType, expectedSignal);
        expect(signalInfo.type).to.equal(expectedType, GetErrorMessage(errorMessage));
        expect(signalInfo.signal).to.equal(expectedSignal, GetErrorMessage(errorMessage));
        if (signalInfo.signal === EOBSOutputSignal.Stop && signalInfo.code !== 0) {
            throw Error(GetErrorMessage(ETestErrorMsg.StreamOutputStoppedWithError, signalInfo.code.toString(), signalInfo.error));
        }
    }

    it('Start Dual Output with advanced recording', async () => {
        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
        recording.signalHandler = (signal) => { obs.signals.push(signal) };

        const recording2 = osn.AdvancedRecordingFactory.create();
        recording2.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording2.format = ERecordingFormat.MP4;
        recording2.useStreamEncoders = false;
        recording2.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording2.overwrite = false;
        recording2.noSpace = false;
        recording2.video = secondContext;
        const track2 = osn.AudioTrackFactory.create(160, 'track2');
        osn.AudioTrackFactory.setAtIndex(track2, 2);
        recording2.signalHandler = (signal) => { obs.signals.push(signal) };

        recording.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        recording2.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        await sleep(1000);

        recording.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        recording2.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        osn.AdvancedRecordingFactory.destroy(recording);

        osn.AdvancedRecordingFactory.destroy(recording2);
    });

    it('Start Dual Output with recording and scene items', async () => {
        const returnSource = osn.Global.getOutputSource(0);

        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
        recording.signalHandler = (signal) => { obs.signals.push(signal) };

        // Getting scene
        let secondSceneName = 'scene_' + randomUUID();
        const scene = osn.SceneFactory.create(secondSceneName);
        osn.Global.setOutputSource(0, scene);

        // Getting source
        let settings: ISettings = {};
        settings = inputSettings.colorSource;
        settings['height'] = 500;
        settings['width'] = 200;
        let secondSourceName = EOBSInputTypes.ColorSource.toString() + '_' + randomUUID();
        const source = osn.InputFactory.create(EOBSInputTypes.ColorSource, secondSourceName, settings);

        // Adding input source to scene to create scene item
        const sceneItem1 = scene.add(source);
        sceneItem1.video = obs.defaultVideoContext;
        sceneItem1.visible = true;
        let position1: IVec2 = { x: 1100, y: 200 };
        sceneItem1.position = position1;

        const sceneItem2 = scene.add(source);
        sceneItem2.video = secondContext;
        sceneItem2.visible = true;
        let position2: IVec2 = { x: 500, y: 1200 };
        sceneItem2.position = position2;

        const recording2 = osn.AdvancedRecordingFactory.create();
        recording2.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording2.format = ERecordingFormat.MP4;
        recording2.useStreamEncoders = false;
        recording2.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder');
        recording2.overwrite = false;
        recording2.noSpace = false;
        recording2.video = secondContext;
        const track2 = osn.AudioTrackFactory.create(160, 'track2');
        osn.AudioTrackFactory.setAtIndex(track2, 2);
        recording2.signalHandler = (signal) => { obs.signals.push(signal) };

        recording.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        recording2.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        await sleep(1000);

        recording.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        recording2.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        osn.AdvancedRecordingFactory.destroy(recording);

        osn.AdvancedRecordingFactory.destroy(recording2);

        osn.Global.setOutputSource(0, returnSource);

        sceneItem1.source.release();
        sceneItem1.remove();
        sceneItem2.remove();
        scene.release();
    });

    it('Start Dual Output with advanced recording and audio scene items', async () => {
        const returnSource = osn.Global.getOutputSource(0);

        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-test-recording-1');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.mixer = 1;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
        recording.signalHandler = (signal) => { obs.signals.push(signal) };

        const recording2 = osn.AdvancedRecordingFactory.create();
        recording2.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording2.format = ERecordingFormat.MP4;
        recording2.useStreamEncoders = false;
        recording2.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-test-recording-2');
        recording2.overwrite = false;
        recording2.noSpace = false;
        recording2.mixer = 2;
        recording2.video = secondContext;
        const track2 = osn.AudioTrackFactory.create(160, 'track2');
        osn.AudioTrackFactory.setAtIndex(track2, 2);
        recording2.signalHandler = (signal) => { obs.signals.push(signal) };

        // Getting scene
        let secondSceneName = 'scene_' + randomUUID();
        const scene = osn.SceneFactory.create(secondSceneName);
        osn.Global.setOutputSource(0, scene);

        // Getting source
        let settings: ISettings = {};
        settings = inputSettings.ffmpegSource;
        settings['volume'] = 100;
        settings['local_file'] = path.join( media_path, "sleek.mp3" );
        settings['looping'] = true;
        let firstSourceName = EOBSInputTypes.FFMPEGSource.toString() + '_' + randomUUID();
        const firstsource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, firstSourceName, settings);

        // Adding input source to scene to create scene item
        const sceneItem1 = scene.add(firstsource);
        sceneItem1.video = obs.defaultVideoContext;
        sceneItem1.visible = true;
        let position1: IVec2 = { x: 1100, y: 200 };
        sceneItem1.position = position1;

        settings['local_file'] = path.join( media_path, "echoes.mp3" );
        let secondSourceName = EOBSInputTypes.FFMPEGSource.toString() + '_' + randomUUID();
        const secondsource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, secondSourceName, settings);

        const sceneItem2 = scene.add(secondsource);
        sceneItem2.video = secondContext;
        sceneItem2.visible = true;
        let position2: IVec2 = { x: 500, y: 1200 };
        sceneItem2.position = position2;

        await sleep(500);

        recording.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        recording2.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        await sleep(1000);

        recording.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        recording2.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        osn.AdvancedRecordingFactory.destroy(recording);

        osn.AdvancedRecordingFactory.destroy(recording2);

        osn.Global.setOutputSource(0, returnSource);

        sceneItem1.source.release();
        sceneItem1.remove();

        sceneItem2.source.release();
        sceneItem2.remove();

        scene.release();
    });


    it('Start Dual Output with simple recording and audio scene items', async () => {
        const returnSource = osn.Global.getOutputSource(0);

        const recording = osn.SimpleRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-test-recording-1');
        recording.audioEncoder = osn.AudioEncoderFactory.create();
        recording.audioEncoder.name = 'audio-encoder-test-recording-1';
        recording.audioEncoder.bitrate = 160;
        recording.overwrite = false;
        recording.noSpace = false;
        recording.quality = ERecordingQuality.HighQuality;
        recording.video = obs.defaultVideoContext;
        const track1 = osn.AudioTrackFactory.create(160, 'track1');
        osn.AudioTrackFactory.setAtIndex(track1, 1);
        recording.signalHandler = (signal) => { obs.signals.push(signal) };

        const recording2 = osn.SimpleRecordingFactory.create();
        recording2.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording2.format = ERecordingFormat.MP4;
        recording2.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-test-recording-2');
        recording2.audioEncoder = osn.AudioEncoderFactory.create();
        recording2.audioEncoder.name = 'audio-encoder-test-recording-2';
        recording2.audioEncoder.bitrate = 160;
        recording2.overwrite = false;
        recording2.noSpace = false;
        recording2.quality = ERecordingQuality.HighQuality;
        recording2.video = secondContext;
        const track2 = osn.AudioTrackFactory.create(160, 'track2');
        osn.AudioTrackFactory.setAtIndex(track2, 2);
        recording2.signalHandler = (signal) => { obs.signals.push(signal) };

        // Getting scene
        let secondSceneName = 'scene_' + randomUUID();
        const scene = osn.SceneFactory.create(secondSceneName);
        osn.Global.setOutputSource(0, scene);

        // Getting source
        let settings: ISettings = {};
        settings = inputSettings.ffmpegSource;
        settings['volume'] = 100;
        settings['local_file'] = path.join( media_path, "sleek.mp3" );
        settings['looping'] = true;
        let firstSourceName = EOBSInputTypes.FFMPEGSource.toString() + '_' + randomUUID();
        const firstsource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, firstSourceName, settings);

        // Adding input source to scene to create scene item
        const sceneItem1 = scene.add(firstsource);
        sceneItem1.video = obs.defaultVideoContext;
        sceneItem1.visible = true;
        let position1: IVec2 = { x: 1100, y: 200 };
        sceneItem1.position = position1;

        settings['local_file'] = path.join( media_path, "echoes.mp3" );
        let secondSourceName = EOBSInputTypes.FFMPEGSource.toString() + '_' + randomUUID();
        const secondsource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, secondSourceName, settings);

        const sceneItem2 = scene.add(secondsource);
        sceneItem2.video = secondContext;
        sceneItem2.visible = true;
        let position2: IVec2 = { x: 500, y: 1200 };
        sceneItem2.position = position2;

        await sleep(500);

        recording.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        recording2.start();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Start, ETestErrorMsg.RecordingOutput);

        await sleep(1000);

        recording.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        recording2.stop();
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stopping, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Stop, ETestErrorMsg.RecordingOutput);
        await handleStreamSignals(EOBSOutputType.Recording, EOBSOutputSignal.Wrote, ETestErrorMsg.RecordingOutput);

        osn.SimpleRecordingFactory.destroy(recording);

        osn.SimpleRecordingFactory.destroy(recording2);

        osn.Global.setOutputSource(0, returnSource);

        sceneItem1.source.release();
        sceneItem1.remove();

        sceneItem2.source.release();
        sceneItem2.remove();

        scene.release();
    });

    it('Start Dual Output with legacy streaming to two services', async () => {
        // Preparing environment
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Simple');
        obs.setSetting(EOBSSettingsCategories.Output, 'StreamEncoder', obs.os === 'win32' ? 'x264' : 'obs_x264');
        obs.setSetting(EOBSSettingsCategories.Output, 'FilePath', path.join(path.normalize(__dirname), '..', 'osnData'));

        // reserve a user account of a second stream
        let secondStreamUserPoolHandler = new UserPoolHandler(testName + "secondStream");
        let userStreamKey = await secondStreamUserPoolHandler.getStreamKey();
        obs.setSetting(EOBSSettingsCategories.StreamSecond, 'key', userStreamKey);

        osn.NodeObs.OBS_service_setVideoInfo(obs.defaultVideoContext, "horizontal");
        osn.NodeObs.OBS_service_setVideoInfo(secondContext, "vertical");

        let signalInfo: IOBSOutputSignalInfo;

        //start first stream
        osn.NodeObs.OBS_service_startStreaming("horizontal");
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Starting, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Activate, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Start, ETestErrorMsg.StreamOutput);

        osn.NodeObs.OBS_service_startStreaming("vertical");
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Starting, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Activate, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Start, ETestErrorMsg.StreamOutput);

        await sleep(500);

        osn.NodeObs.OBS_service_stopStreaming(false, "horizontal");
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stop, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate, ETestErrorMsg.StreamOutput);

        osn.NodeObs.OBS_service_stopStreaming(false, "vertical");
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stop, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate, ETestErrorMsg.StreamOutput);

        await secondStreamUserPoolHandler.releaseUser();
    });

    it('Start Dual Output with legacy streaming to two services and audio sources', async () => {
        // Preparing environment
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Simple');
        obs.setSetting(EOBSSettingsCategories.Output, 'StreamEncoder', obs.os === 'win32' ? 'x264' : 'obs_x264');
        obs.setSetting(EOBSSettingsCategories.Output, 'FilePath', path.join(path.normalize(__dirname), '..', 'osnData'));

        // reserve a user account of a second stream
        let secondStreamUserPoolHandler = new UserPoolHandler(testName + "secondStream");
        let userStreamKey = await secondStreamUserPoolHandler.getStreamKey();
        obs.setSetting(EOBSSettingsCategories.StreamSecond, 'key', userStreamKey);

        osn.NodeObs.OBS_service_setVideoInfo(obs.defaultVideoContext, "horizontal");
        osn.NodeObs.OBS_service_setVideoInfo(secondContext, "vertical");

        let signalInfo: IOBSOutputSignalInfo;
        // Getting scene
        let secondSceneName = 'scene_' + randomUUID();
        const scene = osn.SceneFactory.create(secondSceneName);
        osn.Global.setOutputSource(0, scene);

        // Getting source
        let settings: ISettings = {};
        settings = inputSettings.ffmpegSource;
        settings['volume'] = 100;
        settings['local_file'] = path.join( media_path, "sleek.mp3" );
        settings['looping'] = true;
        let firstSourceName = EOBSInputTypes.FFMPEGSource.toString() + '_' + randomUUID();
        const firstsource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, firstSourceName, settings);

        // Adding input source to scene to create scene item
        const sceneItem1 = scene.add(firstsource);
        sceneItem1.video = obs.defaultVideoContext;
        sceneItem1.visible = true;
        let position1: IVec2 = { x: 1100, y: 200 };
        sceneItem1.position = position1;

        settings['local_file'] = path.join( media_path, "echoes.mp3" );
        let secondSourceName = EOBSInputTypes.FFMPEGSource.toString() + '_' + randomUUID();
        const secondsource = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, secondSourceName, settings);

        const sceneItem2 = scene.add(secondsource);
        sceneItem2.video = secondContext;
        sceneItem2.visible = true;
        let position2: IVec2 = { x: 500, y: 1200 };
        sceneItem2.position = position2;

        // Start first stream
        osn.NodeObs.OBS_service_startStreaming("horizontal");

        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Starting, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Activate, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Start, ETestErrorMsg.StreamOutput);

        // Start second stream
        osn.NodeObs.OBS_service_startStreaming("vertical");

        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Starting, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Activate, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Start, ETestErrorMsg.StreamOutput);

        // Stream for 0.5s
        await sleep(500);

        // Stop first stream
        osn.NodeObs.OBS_service_stopStreaming(false, "horizontal");

        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stop, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate, ETestErrorMsg.StreamOutput);

        // Stop second stream
        osn.NodeObs.OBS_service_stopStreaming(false, "vertical");

        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Stop, ETestErrorMsg.StreamOutput);
        await handleStreamSignals(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate, ETestErrorMsg.StreamOutput);

        await secondStreamUserPoolHandler.releaseUser();
    });
});
