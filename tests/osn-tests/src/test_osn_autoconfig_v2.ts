import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler, IConfigProgress } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';
import { startMockRtmp } from '../util/mock_rtmp';
import { randomUUID } from 'crypto';

const testName = 'osn-autoconfig';

const MOCK_RTMP_PORT = 11935;
const MOCK_RTMP_PORT2 = 11936;

describe(testName, function() {
    this.timeout(120000); // bandwidth tests + apply phase

    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    let videoContext: osn.IVideo = null;

    let sceneName: string;
    let sourceName: string;

    before(async function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);
        obs.connectOutputSignals();

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

    // Pretty-print every resource_usage event collected during a phase. p50 is the
    // typical value, p95 is the sustained ceiling (single-sample spikes from
    // unrelated OS noise are dropped). Mirrors the JSON shape built by
    // resourceWindowToJson() in nodeobs_autoconfig.cpp.
    function logResourceEvents(events: IConfigProgress[]) {
        const fmt = (n: number, d = 1) => (typeof n === 'number' ? n.toFixed(d) : 'n/a');
        for (const ev of events.filter(e => e.event === 'resource_usage')) {
            try {
                const p = JSON.parse(ev.payload || '{}');
                const cpu = `cpu p50/p95=${fmt(p.cpuPct?.p50)}/${fmt(p.cpuPct?.p95)}%`;
                const ram = `ram p50/p95=${fmt(p.procRamMB?.p50, 0)}/${fmt(p.procRamMB?.p95, 0)}MB`;
                const gpu = p.gpu?.available
                    ? `vram p50/p95=${p.gpu.vramUsedMB.p50}/${p.gpu.vramUsedMB.p95}MB (budget ${p.gpu.vramBudgetMB}MB)`
                    : 'gpu=n/a';
                logInfo(testName, `[resource ${p.phase}] samples=${p.sampleCount} dur=${p.durationMs}ms ${cpu} ${ram} ${gpu}`);
            } catch (e) {
                logInfo(testName, `resource_usage parse error: ${(e as Error).message} payload=${ev.payload}`);
            }
        }
    }

    // Pull GetAutoConfigSummary().resourceUsage and log a one-line digest per window.
    // Useful to confirm the summary IPC matches what came over the event stream.
    function logResourceSummary() {
        try {
            const raw = osn.NodeObs.GetAutoConfigSummary() as string;
            if (!raw) return;
            const parsed = JSON.parse(raw);
            const windows = parsed.resourceUsage || [];
            logInfo(testName, `summary.resourceUsage: ${windows.length} window(s)`);
            for (const w of windows) {
                logInfo(testName, `  ${w.phase}: samples=${w.sampleCount} dur=${w.durationMs}ms cpuP95=${w.cpuPct?.p95?.toFixed?.(1)}% ramP95=${w.procRamMB?.p95?.toFixed?.(0)}MB`);
            }
        } catch (e) {
            logInfo(testName, `summary parse error: ${(e as Error).message}`);
        }
    }

    it('Bandwidth test contacts the mock RTMP server', async function() {
        if (obs.isDarwin()) this.skip();

        const mockRtmp = await startMockRtmp(MOCK_RTMP_PORT);
        logInfo(testName, `Mock RTMP listening on 127.0.0.1:${MOCK_RTMP_PORT}`);

        const t = buildStreamingTarget('bw', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        try {
            obs.startAutoconfig([t.stream]);
            osn.NodeObs.StartBandwidthTest();

            const events = await drainUntil(stageDone('bandwidth_test'));
            logInfo(testName, `bandwidth events: ${JSON.stringify(events.filter(e => e.event !== 'resource_usage'))} mock conns=${mockRtmp.getConnections()} bytes=${mockRtmp.getBytes()}`);
            logResourceEvents(events);
            logResourceSummary();

            expect(mockRtmp.getConnections()).to.be.greaterThan(0,
                'Mock RTMP saw no connection — autoconfig did not dial the configured server');

            const errorEvent = events.find((e) => e.event === 'error');
            expect(errorEvent).to.equal(undefined,
                `Bandwidth test failed with: ${errorEvent?.description}`);
        } finally {
            cleanupStreamingTarget(t);
            await mockRtmp.close();
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
            const before = localVideo.video;
            logInfo(testName, `pre-apply video: ${JSON.stringify(before)}`);

            obs.startAutoconfig([t.stream]);

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
            // via obs_set_video_info on the videoId canvas.
            //
            // Note on FPS: libobs's obs_set_video_info only changes output_width /
            // output_height at runtime. fps_num is locked once the video pipeline is
            // alive and only updates on a destroy+recreate of the video context. We
            // therefore assert width/height landed but not fpsNum — the frontend has
            // to drop and recreate the canvas to take a new framerate.
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
        // Empty target list — server should reject with no_streaming_targets_provided
        // during bandwidth test.
        obs.startAutoconfig([]);
        osn.NodeObs.StartBandwidthTest();

        const events = await drainUntil(stageDone('bandwidth_test'));
        // Need at least one error event; description identifies the missing target.
        const errorEvent = events.find((e) => e.event === 'error');
        expect(errorEvent).to.not.equal(undefined, 'Expected an error event');
        expect(errorEvent.description).to.equal('no_streaming_targets_provided');
    });

    it('TerminateAutoConfig mid-bandwidth-test leaves live values untouched', async function() {
        if (obs.isDarwin()) this.skip();

        const mockRtmp = await startMockRtmp(MOCK_RTMP_PORT);
        const t = buildStreamingTarget('cancel', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        const beforeBitrate = t.videoEncoder.settings['bitrate'] as number;
        const beforeServer = t.service.settings['server'] as string;

        try {
            obs.startAutoconfig([t.stream]);
            osn.NodeObs.StartBandwidthTest();

            await sleep(500);
            osn.NodeObs.TerminateAutoConfig();

            // TerminateAutoConfig sets the cancel flag and kills the client-side
            // polling worker. The bandwidth thread may still be winding down
            // asynchronously — give it a moment, then verify values are untouched.
            // We intentionally do NOT drainUntil() here because the worker that
            // delivers events has already been stopped.
            await sleep(1000);

            expect(t.videoEncoder.settings['bitrate']).to.equal(beforeBitrate, 'Bitrate must not change on cancel');
            expect(t.service.settings['server']).to.equal(beforeServer, 'Server URL must not change on cancel');
        } finally {
            cleanupStreamingTarget(t);
            await mockRtmp.close();
        }
    });

    // ---- Encoder-phase tests (resource sampling exercise) ----

    it('Stream encoder test surfaces resource_usage', async function() {
        if (obs.isDarwin()) this.skip();

        const t = buildStreamingTarget('senc', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        try {
            obs.startAutoconfig([t.stream]);

            osn.NodeObs.StartStreamEncoderTest();
            const events = await drainUntil(stageDone('runContext.streamingEncoder_test'));
            logInfo(testName, `stream-encoder events: ${JSON.stringify(events.filter(e => e.event !== 'resource_usage'))}`);
            logResourceEvents(events);
            logResourceSummary();

            const errorEvent = events.find((e) => e.event === 'error');
            expect(errorEvent).to.equal(undefined,
                `Stream encoder test failed with: ${errorEvent?.description}`);

            const resEvents = events.filter(e => e.event === 'resource_usage');
            expect(resEvents.length).to.be.greaterThan(0, 'expected at least one resource_usage event');
            for (const r of resEvents) {
                const p = JSON.parse(r.payload || '{}');
                expect(p.sampleCount).to.be.greaterThan(0, `resource window for ${p.phase} had no samples`);
            }
        } finally {
            cleanupStreamingTarget(t);
        }
    });

    it('Recording encoder test surfaces resource_usage', async function() {
        if (obs.isDarwin()) this.skip();

        const t = buildStreamingTarget('renc', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        try {
            obs.startAutoconfig([t.stream]);

            osn.NodeObs.StartRecordingEncoderTest();
            const events = await drainUntil(stageDone('runContext.recordingEncoder_test'));
            logInfo(testName, `recording-encoder events: ${JSON.stringify(events.filter(e => e.event !== 'resource_usage'))}`);
            logResourceEvents(events);
            logResourceSummary();

            const errorEvent = events.find((e) => e.event === 'error');
            expect(errorEvent).to.equal(undefined,
                `Recording encoder test failed with: ${errorEvent?.description}`);

            const resEvents = events.filter(e => e.event === 'resource_usage');
            expect(resEvents.length).to.be.greaterThan(0, 'expected at least one resource_usage event');
        } finally {
            cleanupStreamingTarget(t);
        }
    });

    // ---- Dual-target (Dual Output) tests ----

    it('Dual-target bandwidth test contacts both mock RTMP servers', async function() {
        if (obs.isDarwin()) this.skip();

        const mockRtmp1 = await startMockRtmp(MOCK_RTMP_PORT);
        const mockRtmp2 = await startMockRtmp(MOCK_RTMP_PORT2);
        logInfo(testName, `Mock RTMP listening on ports ${MOCK_RTMP_PORT} and ${MOCK_RTMP_PORT2}`);

        const t1 = buildStreamingTarget('dual-bw1', `rtmp://127.0.0.1:${MOCK_RTMP_PORT}/live`);
        const t2 = buildStreamingTarget('dual-bw2', `rtmp://127.0.0.1:${MOCK_RTMP_PORT2}/live`);
        try {
            obs.startAutoconfig([t1.stream, t2.stream]);
            osn.NodeObs.StartBandwidthTest();

            const events = await drainUntil(stageDone('bandwidth_test'));
            logInfo(testName, `dual-bw events: ${JSON.stringify(events.filter(e => e.event !== 'resource_usage'))} mock1 conns=${mockRtmp1.getConnections()} mock2 conns=${mockRtmp2.getConnections()}`);
            logResourceEvents(events);
            logResourceSummary();

            expect(mockRtmp1.getConnections()).to.be.greaterThan(0,
                'Mock RTMP #1 saw no connection — primary target was not tested');
            expect(mockRtmp2.getConnections()).to.be.greaterThan(0,
                'Mock RTMP #2 saw no connection — secondary target was not tested');
        } finally {
            cleanupStreamingTarget(t1);
            cleanupStreamingTarget(t2);
            await mockRtmp1.close();
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
            obs.startAutoconfig([t1.stream, t2.stream]);

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
