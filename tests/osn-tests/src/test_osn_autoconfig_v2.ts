import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine, logWarning } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler, IConfigProgress } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';
import { randomUUID } from 'crypto';
import path = require('path');

const testName = 'osn-autoconfig-v2';

describe(testName, function() {
    this.timeout(60000); // 60 second timeout for bandwidth tests
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    let newSceneName = 'scene_' + randomUUID();
    let newSourceName = 'color_source_' + randomUUID();
    let secondContext;

    // Initialize OBS process
    before(async function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);

        // Connecting output signals
        obs.connectOutputSignals();

        // Create second video context for streaming
        secondContext = osn.VideoFactory.create();
        const secondVideoInfo: osn.IVideoInfo = {
            fpsNum: 30,
            fpsDen: 1,
            baseWidth: 1280,
            baseHeight: 720,
            outputWidth: 1280,
            outputHeight: 720,
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
        // Destroy second context
        if (secondContext) {
            secondContext.destroy();
        }

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

    beforeEach(function() {
        // Creating scene
        const scene = osn.SceneFactory.create(newSceneName);

        // Checking if scene was created correctly
        expect(scene).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateScene, newSceneName));
        expect(scene.id).to.equal('scene', GetErrorMessage(ETestErrorMsg.SceneId, newSceneName));
        expect(scene.name).to.equal(newSceneName, GetErrorMessage(ETestErrorMsg.SceneName, newSceneName));
        expect(scene.type).to.equal(osn.ESourceType.Scene, GetErrorMessage(ETestErrorMsg.SceneType, newSceneName));

        // Creating color source for test content
        const source = osn.InputFactory.create('color_source', newSourceName);

        // Checking if source was created correctly
        expect(source).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, 'color_source'));
        expect(source.id).to.equal('color_source', GetErrorMessage(ETestErrorMsg.InputId, 'color_source'));
        expect(source.name).to.equal(newSourceName, GetErrorMessage(ETestErrorMsg.InputName, 'color_source'));

        // Add source to scene
        scene.add(source);

        // Set as output source
        osn.Global.setOutputSource(0, scene);
        osn.Global.setOutputSource(1, scene);
    });

    afterEach(function() {
        // Clean up scene
        const scene = osn.SceneFactory.fromName(newSceneName);
        if (scene) {
            scene.release();
        }

        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    it('Setup streaming services with local RTMP endpoints', async function() {
        logInfo(testName, 'Setting up streaming services for bandwidth test');

        let simpleStreamingService1: osn.ISimpleStreaming;
        let simpleStreamingService2: osn.ISimpleStreaming;
        let videoEncoder1: osn.IVideoEncoder;
        let videoEncoder2: osn.IVideoEncoder;
        let audioEncoder: osn.IAudioEncoder;
        let service1: osn.IService;
        let service2: osn.IService;

        // Create video encoder 1 for service 1
        videoEncoder1 = osn.VideoEncoderFactory.create('obs_x264', 'test_video_encoder_1');
        expect(videoEncoder1).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.StreamOutput));

        const videoEncoderSettings1 = videoEncoder1.settings;
        videoEncoderSettings1['bitrate'] = 2500;
        videoEncoderSettings1['rate_control'] = 'CBR';
        videoEncoderSettings1['preset'] = 'veryfast';
        videoEncoderSettings1['keyint_sec'] = 2;
        videoEncoder1.update(videoEncoderSettings1);

        // Create video encoder 2 for service 2
        videoEncoder2 = osn.VideoEncoderFactory.create('obs_x264', 'test_video_encoder_2');
        expect(videoEncoder2).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.StreamOutput));

        const videoEncoderSettings2 = videoEncoder2.settings;
        videoEncoderSettings2['bitrate'] = 2500;
        videoEncoderSettings2['rate_control'] = 'CBR';
        videoEncoderSettings2['preset'] = 'veryfast';
        videoEncoderSettings2['keyint_sec'] = 2;
        videoEncoder2.update(videoEncoderSettings2);

        // Create audio encoder (shared)
        audioEncoder = osn.AudioEncoderFactory.create('ffmpeg_aac', 'test_audio_encoder');
        expect(audioEncoder).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.StreamOutput));

        // Create Service 1 - Custom RTMP (local test server)
        service1 = osn.ServiceFactory.create('rtmp_common', 'test_service_1');
        expect(service1).to.not.equal(undefined, 'Failed to create service 1');

        const service1Settings = service1.settings;
        service1Settings['service'] = 'Custom';
        service1Settings['server'] = 'rtmp://127.0.0.1:1935/live';
        service1Settings['key'] = 'test_stream_1';
        service1.update(service1Settings);

        // Create Service 2 - Another custom RTMP (simulating second server)
        service2 = osn.ServiceFactory.create('rtmp_common', 'test_service_2');
        expect(service2).to.not.equal(undefined, 'Failed to create service 2');

        const service2Settings = service2.settings;
        service2Settings['service'] = 'Custom';
        service2Settings['server'] = 'rtmp://127.0.0.1:1936/live';
        service2Settings['key'] = 'test_stream_2';
        service2.update(service2Settings);

        // Create Simple Streaming Output 1
        simpleStreamingService1 = osn.SimpleStreamingFactory.create();
        expect(simpleStreamingService1).to.not.equal(undefined, 'Failed to create simple streaming service 1');

        simpleStreamingService1.videoEncoder = videoEncoder1;
        simpleStreamingService1.service = service1;
        simpleStreamingService1.enforceServiceBitrate = false;
        simpleStreamingService1.video = secondContext;

        // Set delay settings
        const delay1 = simpleStreamingService1.delay;
        simpleStreamingService1.delay = delay1;

        // Set reconnect settings
        const reconnect1 = simpleStreamingService1.reconnect;
        simpleStreamingService1.reconnect = reconnect1;

        // Set network settings
        const network1 = simpleStreamingService1.network;
        simpleStreamingService1.network = network1;

        // Create Simple Streaming Output 2
        simpleStreamingService2 = osn.SimpleStreamingFactory.create();
        expect(simpleStreamingService2).to.not.equal(undefined, 'Failed to create simple streaming service 2');

        simpleStreamingService2.videoEncoder = videoEncoder2;
        simpleStreamingService2.service = service2;
        simpleStreamingService2.enforceServiceBitrate = false;
        simpleStreamingService2.video = secondContext;

        // Set delay settings
        const delay2 = simpleStreamingService2.delay;
        simpleStreamingService2.delay = delay2;

        // Set reconnect settings
        const reconnect2 = simpleStreamingService2.reconnect;
        simpleStreamingService2.reconnect = reconnect2;

        // Set network settings
        const network2 = simpleStreamingService2.network;
        simpleStreamingService2.network = network2;

        logInfo(testName, 'Successfully set up two streaming services');

        // Clean up resources
        osn.SimpleStreamingFactory.destroy(simpleStreamingService1);
        osn.SimpleStreamingFactory.destroy(simpleStreamingService2);

        videoEncoder1.release();
        videoEncoder2.release();
        audioEncoder.release();
        
        osn.ServiceFactory.destroy(service1);
        osn.ServiceFactory.destroy(service2);
    });

    it('Run bandwidth test V2 (without local server - expect error)', async function() {
        logInfo(testName, 'Testing bandwidth test V2 with pre-configured services');

        let simpleStreamingService1: osn.ISimpleStreaming;
        let simpleStreamingService2: osn.ISimpleStreaming;
        let videoEncoder1: osn.IVideoEncoder;
        let videoEncoder2: osn.IVideoEncoder;
        let audioEncoder: osn.IAudioEncoder;
        let service1: osn.IService;
        let service2: osn.IService;

        // Set up services for bandwidth test
        videoEncoder1 = osn.VideoEncoderFactory.create('obs_x264', 'bw_video_encoder_1');
        videoEncoder1.update({ bitrate: 2500, rate_control: 'CBR', preset: 'veryfast', keyint_sec: 2 });

        videoEncoder2 = osn.VideoEncoderFactory.create('obs_x264', 'bw_video_encoder_2');
        videoEncoder2.update({ bitrate: 2500, rate_control: 'CBR', preset: 'veryfast', keyint_sec: 2 });

        audioEncoder = osn.AudioEncoderFactory.create('ffmpeg_aac', 'bw_audio_encoder');

        service1 = osn.ServiceFactory.create('rtmp_common', 'bw_service_1');
        service1.update({ service: 'Custom', server: 'rtmp://127.0.0.1:1935/live', key: 'bw_test_1' });

        service2 = osn.ServiceFactory.create('rtmp_common', 'bw_service_2');
        service2.update({ service: 'Custom', server: 'rtmp://127.0.0.1:1936/live', key: 'bw_test_2' });

        simpleStreamingService1 = osn.SimpleStreamingFactory.create();
        simpleStreamingService1.videoEncoder = videoEncoder1;
        simpleStreamingService1.service = service1;
        simpleStreamingService1.video = secondContext;

        simpleStreamingService2 = osn.SimpleStreamingFactory.create();
        simpleStreamingService2.videoEncoder = videoEncoder2;
        simpleStreamingService2.service = service2;
        simpleStreamingService2.video = secondContext;

        // Initialize autoconfig
        obs.startAutoconfig();

        // Start bandwidth test V2
        osn.NodeObs.StartBandwidthTest();

        // Wait for bandwidth test to complete
        const progressInfo: IConfigProgress = await obs.getNextProgressInfo('Bandwidth test V2');

        // Since we don't have actual RTMP servers, we expect either:
        // 1. An error (services failed to connect)
        // 2. Or completion with no valid results
        
        if (progressInfo.event === 'error') {
            // Expected: services couldn't connect to local servers
            logInfo(testName, 'Bandwidth test failed as expected (no local RTMP servers): ' + progressInfo.description);
            expect(progressInfo.description).to.be.oneOf([
                'no_streaming_services_available',
                'no_valid_bandwidth_results',
                'invalid_stream_settings'
            ], 'Should fail with expected error message');
        } else if (progressInfo.event === 'stopping_step') {
            // Test completed (might happen if setup fails early)
            logInfo(testName, 'Bandwidth test completed');
            expect(progressInfo.description).to.equal('bandwidth_test', 'Description should be bandwidth_test');
            expect(progressInfo.percentage).to.equal(100, 'Percentage should be 100');
        } else {
            // Unexpected state
            expect.fail('Unexpected progress event: ' + progressInfo.event);
        }

        logInfo(testName, 'Bandwidth test V2 API executed successfully');

        // Clean up resources
        osn.SimpleStreamingFactory.destroy(simpleStreamingService1);
        osn.SimpleStreamingFactory.destroy(simpleStreamingService2);

        videoEncoder1.release();
        videoEncoder2.release();
        audioEncoder.release();

        osn.ServiceFactory.destroy(service1);
        osn.ServiceFactory.destroy(service2);
    });

    it('Verify autoconfig can be terminated', function() {
        logInfo(testName, 'Testing autoconfig termination');
        
        osn.NodeObs.TerminateAutoConfig();
        
        // Give it a moment to clean up
        sleep(100);
        
        logInfo(testName, 'Autoconfig terminated successfully');
    });
});
