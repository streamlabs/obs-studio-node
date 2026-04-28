import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler, IConfigProgress } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';
import { startMockRtmp, IMockRtmp } from '../util/mock_rtmp';
import { randomUUID } from 'crypto';

const testName = 'osn-autoconfig';

// Local mock RTMP server port. Picked above the 1024 reserved range and outside
// common dev-tool defaults; bandwidth test will dial 127.0.0.1 here.
const MOCK_RTMP_PORT = 11935;

describe(testName, function() {
    this.timeout(120000); // bandwidth tests + apply phase

    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    let mockRtmp: IMockRtmp = null;
    let videoContext: osn.IVideo = null;

    let sceneName: string;
    let sourceName: string;

    before(async function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);
        obs.connectOutputSignals();

        // Boot the mock RTMP listener once for all bandwidth-test cases.
        mockRtmp = await startMockRtmp(MOCK_RTMP_PORT);
        logInfo(testName, `Mock RTMP listening on 127.0.0.1:${MOCK_RTMP_PORT}`);

        // Dedicated video context so we can assert resolution/FPS got applied
        // without disturbing the handler's defaultVideoContext.
        videoContext = osn.VideoFactory.create();
        videoContext.video = {
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
            fpsType: osn.EFPSType.Fractional,
        };
    });

    after(async function() {
        if (videoContext) videoContext.destroy();
        if (mockRtmp) await mockRtmp.close();

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
        // Each case gets its own scene/source pair so failed cleanup in one case
        // can't poison the next.
        sceneName = 'scene_' + randomUUID();
        sourceName = 'color_source_' + randomUUID();
        const scene = osn.SceneFactory.create(sceneName);
        const source = osn.InputFactory.create('color_source', sourceName);
        scene.add(source);
        osn.Global.setOutputSource(0, scene);
    });

    afterEach(function() {
        const scene = osn.SceneFactory.fromName(sceneName);
        if (scene) scene.release();
        if (this.currentTest.state === 'failed') hasTestFailed = true;
    });

    // Build a SimpleStreaming target wired to the mock RTMP server. Returned objects
    // must be cleaned up by the caller via cleanupStreamingTarget().
    function buildStreamingTarget(label: string, server: string) {
        const videoEncoder = osn.VideoEncoderFactory.create('obs_x264', `enc-${label}`);
        videoEncoder.update({ bitrate: 2500, rate_control: 'CBR', preset: 'veryfast', keyint_sec: 2 });
        const audioEncoder = osn.AudioEncoderFactory.create('ffmpeg_aac', `aenc-${label}`);
        const service = osn.ServiceFactory.create('rtmp_common', `svc-${label}`);
        service.update({ service: 'Custom', server, key: `key-${label}` });

        const stream = osn.SimpleStreamingFactory.create();
        stream.videoEncoder = videoEncoder;
        stream.audioEncoder = audioEncoder;
        stream.service = service;
        stream.video = videoContext;
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.enforceServiceBitrate = false;
        stream.signalHandler = (signal) => obs.signals.push(signal);
        return { stream, service, videoEncoder, audioEncoder };
    }

    function cleanupStreamingTarget(t: ReturnType<typeof buildStreamingTarget>) {
        osn.SimpleStreamingFactory.destroy(t.stream);
        osn.ServiceFactory.destroy(t.service);
        t.videoEncoder.release();
        t.audioEncoder.release();
    }

    // Drains autoconfig events until the predicate returns true, an error event
    // arrives, or the deadline elapses. Returns the full list of events seen.
    async function drainUntil(stop: (ev: IConfigProgress) => boolean): Promise<IConfigProgress[]> {
        const seen: IConfigProgress[] = [];
        const deadline = Date.now() + 60000;
        while (Date.now() < deadline) {
            const ev = await obs.getNextProgressInfo('autoconfig');
            seen.push(ev);
            if (ev.event === 'error' || stop(ev)) return seen;
        }
        throw new Error('autoconfig drain timeout');
    }
    const stageDone = (description: string) =>
        (ev: IConfigProgress) => ev.event === 'stopping_step' && ev.description === description;
    const isDone = (ev: IConfigProgress) => ev.event === 'done';

    it('Bandwidth test contacts the mock RTMP server', async function() {
        // This test only validates the orchestration of the bandwidth path: that the
        // server discovers the registered SimpleStreaming, that the bandwidth thread
        // dials the configured RTMP server, and that bytes flow over the socket. It
        // does NOT assert a final bitrate, because the minimal mock only handles the
        // initial RTMP handshake — libobs never enters the "publishing" state, so
        // obs_output_get_total_bytes stays 0 and the autoconfig naturally returns
        // "no_valid_bandwidth_results". Bitrate-application correctness is covered
        // by the dedicated "Apply phase lands defaults" test below using a controlled
        // input path (SetDefaultSettings) instead of a synthesized RTMP exchange.
        if (obs.isDarwin()) this.skip();

        const connectionsBefore = mockRtmp.getConnections();
        const bytesBefore = mockRtmp.getBytes();

        const t = buildStreamingTarget('bw', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        try {
            obs.startAutoconfig();
            osn.NodeObs.StartBandwidthTest();

            const events = await drainUntil(stageDone('bandwidth_test'));
            logInfo(testName, `bandwidth events: ${JSON.stringify(events)} mock conns=${mockRtmp.getConnections() - connectionsBefore} bytes=${mockRtmp.getBytes() - bytesBefore}`);

            expect(mockRtmp.getConnections() - connectionsBefore).to.be.greaterThan(0,
                'Mock RTMP saw no new connection — autoconfig did not dial the configured server');
            expect(mockRtmp.getBytes() - bytesBefore).to.be.greaterThan(0,
                'Mock RTMP saw no new bytes — RTMP handshake did not even start');
        } finally {
            cleanupStreamingTarget(t);
        }
    });

    it('Apply phase lands defaults on live osn objects', async function() {
        // SetDefaultSettings populates runContext with hardcoded, known values
        // (idealResolutionCX=1280, CY=720, FPSNum=30, idealBitrate=4500). SaveSettings
        // then runs applyResults() which discovers all registered streaming targets
        // and pushes those values into them. After 'done', the live objects' Get*
        // methods should report the applied values.

        // Use a fresh video context here. The shared `videoContext` from before()
        // gets touched by other tests' streaming pipelines and ends up with libobs
        // applying its own canonicalisation to fps_num — easier to start clean.
        const localVideo = osn.VideoFactory.create();
        localVideo.video = {
            fpsNum: 60, fpsDen: 1,
            baseWidth: 1920, baseHeight: 1080,
            outputWidth: 1920, outputHeight: 1080,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Full,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: osn.EFPSType.Fractional,
        };

        const t = buildStreamingTarget('apply', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        // Re-point this stream's video at the local context.
        t.stream.video = localVideo;
        try {
            localVideo.refresh();
            const before = localVideo.video;
            logInfo(testName, `pre-apply video: ${JSON.stringify(before)}`);

            obs.startAutoconfig();

            osn.NodeObs.StartSetDefaultSettings();
            await drainUntil(stageDone('setting_default_settings'));

            osn.NodeObs.StartSaveSettings();
            const events = await drainUntil(isDone);
            logInfo(testName, `apply events: ${JSON.stringify(events)}`);
            const terminal = events[events.length - 1];
            expect(terminal.event).to.equal('done', `Expected terminal 'done', got '${terminal.event}/${terminal.description}'`);

            // SetDefaultSettings sets idealBitrate=4500 — applyResults forwards that
            // to the videoEncoder via obs_encoder_update, capped by EstimateUpperBitrate
            // for the chosen resolution. For 1280x720@30 the cap is ~3000 kbps, so the
            // applied value will be in (initial=2500, idealBitrate=4500].
            const appliedBitrate = t.videoEncoder.settings['bitrate'] as number;
            expect(appliedBitrate).to.be.greaterThan(2500, `bitrate did not change from initial 2500: got ${appliedBitrate}`);
            expect(appliedBitrate).to.be.lessThanOrEqual(4500, `bitrate exceeded idealBitrate 4500: got ${appliedBitrate}`);

            // SetDefaultSettings sets idealResolution 1280x720 — applyResults forwards
            // via obs_set_video_info on the videoId canvas. The client-side Video.get
            // caches its last fetched snapshot; refresh() drops the cache so the next
            // read hits the (newly mutated) server-side state.
            //
            // Note on FPS: libobs's obs_set_video_info only changes output_width /
            // output_height at runtime. fps_num is locked once the video pipeline is
            // alive and only updates on a destroy+recreate of the video context. We
            // therefore assert width/height landed but not fpsNum — the frontend has
            // to drop and recreate the canvas to take a new framerate.
            localVideo.refresh();
            const v = localVideo.video;
            logInfo(testName, `post-apply video: ${JSON.stringify(v)}`);
            expect(v.outputWidth).to.equal(1280, `expected outputWidth 1280, got ${v.outputWidth}`);
            expect(v.outputHeight).to.equal(720, `expected outputHeight 720, got ${v.outputHeight}`);
        } finally {
            cleanupStreamingTarget(t);
            localVideo.destroy();
        }
    });

    it('Autoconfig with no streaming target reports an error event', async function() {
        // No streaming objects registered in the manager — server should reject
        // with no_streaming_target_provided during bandwidth test.
        obs.startAutoconfig();
        osn.NodeObs.StartBandwidthTest();

        const events = await drainUntil(stageDone('bandwidth_test'));
        // Need at least one error event; description identifies the missing target.
        const errorEvent = events.find((e) => e.event === 'error');
        expect(errorEvent).to.not.equal(undefined, 'Expected an error event');
        expect(errorEvent.description).to.equal('no_streaming_targets_registered');
    });

    it('TerminateAutoConfig mid-bandwidth-test leaves live values untouched', async function() {
        if (obs.isDarwin()) this.skip();

        const t = buildStreamingTarget('cancel', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        const beforeBitrate = t.videoEncoder.settings['bitrate'] as number;
        const beforeServer = t.service.settings['server'] as string;

        try {
            obs.startAutoconfig();
            osn.NodeObs.StartBandwidthTest();

            // Give the test a moment to actually start streaming, then cancel.
            await sleep(500);
            osn.NodeObs.TerminateAutoConfig();

            // Drain — cancelled run still emits stopping_step "bandwidth_test" because
            // the cancel arm of the loop drops out cleanly without emitting an explicit
            // error event (gotError is true but no sendErrorMessage call). What we care
            // about is that no apply happened, which we assert via getter values below.
            const events = await drainUntil(stageDone('bandwidth_test'));
            logInfo(testName, `cancel events: ${JSON.stringify(events)}`);

            // No SaveSettings was called — values must not have been mutated.
            expect(t.videoEncoder.settings['bitrate']).to.equal(beforeBitrate, 'Bitrate must not change on cancel');
            expect(t.service.settings['server']).to.equal(beforeServer, 'Server URL must not change on cancel');
        } finally {
            cleanupStreamingTarget(t);
        }
    });

    // ---- Dual-target (Dual Output) tests ----

    const MOCK_RTMP_PORT2 = 11936;

    it('Dual-target bandwidth test contacts both mock RTMP servers', async function() {
        if (obs.isDarwin()) this.skip();

        const mockRtmp2 = await startMockRtmp(MOCK_RTMP_PORT2);
        try {
            const connsBefore1 = mockRtmp.getConnections();
            const connsBefore2 = mockRtmp2.getConnections();

            const t1 = buildStreamingTarget('dual-bw1', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
            const t2 = buildStreamingTarget('dual-bw2', `rtmp://127.0.0.1:${MOCK_RTMP_PORT2}/live`);
            try {
                obs.startAutoconfig();
                osn.NodeObs.StartBandwidthTest();

                const events = await drainUntil(stageDone('bandwidth_test'));
                logInfo(testName, `dual-bw events: ${JSON.stringify(events)} mock1 conns=${mockRtmp.getConnections() - connsBefore1} mock2 conns=${mockRtmp2.getConnections() - connsBefore2}`);

                expect(mockRtmp.getConnections() - connsBefore1).to.be.greaterThan(0,
                    'Mock RTMP #1 saw no connection — primary target was not tested');
                expect(mockRtmp2.getConnections() - connsBefore2).to.be.greaterThan(0,
                    'Mock RTMP #2 saw no connection — secondary target was not tested');
            } finally {
                cleanupStreamingTarget(t1);
                cleanupStreamingTarget(t2);
            }
        } finally {
            await mockRtmp2.close();
        }
    });

    it('Apply phase with dual targets applies per-target values', async function() {
        const localVideo = osn.VideoFactory.create();
        localVideo.video = {
            fpsNum: 60, fpsDen: 1,
            baseWidth: 1920, baseHeight: 1080,
            outputWidth: 1920, outputHeight: 1080,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Full,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: osn.EFPSType.Fractional,
        };

        const t1 = buildStreamingTarget('dual-apply1', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        const t2 = buildStreamingTarget('dual-apply2', `rtmp://127.0.0.1:${MOCK_RTMP_PORT2}/live`);
        t1.stream.video = localVideo;
        t2.stream.video = localVideo;

        try {
            obs.startAutoconfig();

            osn.NodeObs.StartSetDefaultSettings();
            await drainUntil(stageDone('setting_default_settings'));

            osn.NodeObs.StartSaveSettings();
            const events = await drainUntil(isDone);
            logInfo(testName, `dual-apply events: ${JSON.stringify(events)}`);

            const terminal = events[events.length - 1];
            expect(terminal.event).to.equal('done', `Expected terminal 'done', got '${terminal.event}/${terminal.description}'`);

            // Both targets should have received a bitrate update from applyResults.
            const br1 = t1.videoEncoder.settings['bitrate'] as number;
            const br2 = t2.videoEncoder.settings['bitrate'] as number;
            expect(br1).to.be.greaterThan(2500, `target1 bitrate not updated: got ${br1}`);
            expect(br2).to.be.greaterThan(2500, `target2 bitrate not updated: got ${br2}`);

            // Video canvas should have been updated too.
            localVideo.refresh();
            const v = localVideo.video;
            expect(v.outputWidth).to.equal(1280, `expected outputWidth 1280, got ${v.outputWidth}`);
            expect(v.outputHeight).to.equal(720, `expected outputHeight 720, got ${v.outputHeight}`);
        } finally {
            cleanupStreamingTarget(t1);
            cleanupStreamingTarget(t2);
            localVideo.destroy();
        }
    });
});
