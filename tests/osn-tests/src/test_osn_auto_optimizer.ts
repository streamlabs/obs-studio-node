import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import type {
    IAutoConfigEvent,
    IAutoConfigRequest,
    IAutoConfigResult,
} from '../../../js/module';
import * as net from 'net';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';

const testName = 'osn-auto-optimizer';
const mockPort = 11937;

describe(testName, function() {
    this.timeout(340000);

    let obs: OBSHandler;

    async function startConnectionSink(port: number) {
        let connections = 0;
        let bytes = 0;
        const server = net.createServer(socket => {
            connections++;
            socket.on('data', chunk => bytes += chunk.length);
        });
        await new Promise<void>((resolve, reject) => {
            server.once('error', reject);
            server.listen(port, '127.0.0.1', resolve);
        });
        return {
            getConnections: () => connections,
            getBytes: () => bytes,
            close: () => new Promise<void>(resolve => server.close(() => resolve())),
        };
    }

    before(function() {
        deleteConfigFiles();
        obs = new OBSHandler(testName);
    });

    after(function() {
        if (obs) {
            obs.shutdown();
            obs = null;
        }
        deleteConfigFiles();
    });

    type AutoConfigOutputRequest = IAutoConfigRequest['outputs'][number];
    interface AutoConfigRun {
        readonly result: Promise<IAutoConfigResult>;
        confirmProbeIngest(probeId: string, received: boolean): void;
        cancel(): Promise<void>;
    }
    const autoConfig = osn.NodeObs.AutoConfig as unknown as {
        run(request: IAutoConfigRequest, onProgress: (event: IAutoConfigEvent) => void): AutoConfigRun;
    };

    function output(overrides: Partial<AutoConfigOutputRequest> = {}): AutoConfigOutputRequest {
        return {
            outputId: 'primary',
            display: 'horizontal',
            outputKind: 'standard',
            destinations: ['custom'],
            current: {
                canvasId: obs.defaultVideoContext.canvasId,
                width: 1280,
                height: 720,
                fpsNum: 30,
                fpsDen: 1,
                bitrateKbps: 2500,
                encoderId: 'obs_x264',
                preset: 'veryfast',
            },
            ...overrides,
        };
    }

    function pairedEnhancedBroadcastingRequest(primaryCanvasId: number, additionalCanvasId: number): IAutoConfigRequest {
        return {
            streamSetup: 'enhanced-broadcasting',
            outputs: [output({
                display: 'both',
                outputKind: 'twitch-enhanced-broadcasting',
                destinations: ['twitch'],
                current: { ...output().current, canvasId: primaryCanvasId },
                additionalVideo: {
                    display: 'vertical',
                    current: {
                        ...output().current,
                        canvasId: additionalCanvasId,
                        width: 720,
                        height: 1280,
                        fpsNum: 60,
                    },
                },
                probes: [{
                    id: 'enhanced-broadcasting-primary',
                    kind: 'twitch-enhanced-broadcasting',
                    streamKey: 'integration-test-key',
                }],
            })],
        };
    }

    function enhancedBroadcastingDualOutputRequest(primaryCanvasId: number, additionalCanvasId: number): IAutoConfigRequest {
        const paired = pairedEnhancedBroadcastingRequest(primaryCanvasId, additionalCanvasId);
        const enhanced = paired.outputs[0];
        enhanced.outputId = 'enhanced';
        return {
            ...paired,
            streamSetup: 'enhanced-broadcasting-dual-output',
            outputs: [
                enhanced,
                output({
                    outputId: 'horizontal-companion',
                    display: 'horizontal',
                    outputKind: 'standard',
                    destinations: ['kick'],
                    current: { ...enhanced.current },
                }),
                output({
                    outputId: 'vertical-companion',
                    display: 'vertical',
                    outputKind: 'standard',
                    destinations: ['facebook'],
                    current: { ...enhanced.additionalVideo!.current },
                }),
            ],
        };
    }

    async function startSessionAtHardwareAttempt(): Promise<AutoConfigRun> {
        let nativeRun: AutoConfigRun | null = null;
        let timeout: ReturnType<typeof setTimeout>;
        const hardwareAttemptStarted = new Promise<void>((resolve, reject) => {
            timeout = setTimeout(() => reject(new Error('Timed out waiting for the Auto Optimizer benchmark workload')), 15000);
            const onEvent = (event: IAutoConfigEvent) => {
                if (event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_encoder_surfaces' ||
                    event.code === 'hardware_testing_x264') {
                    clearTimeout(timeout);
                    resolve();
                } else if (event.type === 'complete' || event.type === 'cancelled') {
                    clearTimeout(timeout);
                    reject(new Error(`Auto Optimizer stopped before starting a benchmark workload: ${event.code}`));
                }
            };

            nativeRun = autoConfig.run(
                {
                    streamSetup: 'custom-rtmp',
                    outputs: [output()],
                } as IAutoConfigRequest,
                onEvent,
            );
        });

        try {
            await hardwareAttemptStarted;
            return nativeRun!;
        } catch (error) {
            if (nativeRun)
                await nativeRun.cancel().catch(() => undefined);
            throw error;
        }
    }

    async function run(request: IAutoConfigRequest): Promise<{
        events: IAutoConfigEvent[];
        result: IAutoConfigResult;
    }> {
        const events: IAutoConfigEvent[] = [];
        const nativeRun = autoConfig.run(request, event => events.push(event));
        let timeout: ReturnType<typeof setTimeout>;
        const timedResult = new Promise<IAutoConfigResult>((_, reject) => {
            timeout = setTimeout(() => {
                nativeRun.cancel().then(
                    () => reject(new Error('Auto Optimizer session timed out')),
                    reject,
                );
            }, 330000);
        });
        try {
            const result = await Promise.race([nativeRun.result, timedResult]);
            for (const event of events) {
                for (const field of ['schemaVersion', 'sessionId', 'sequence', 'provider', 'probeId', 'legId'])
                    expect(event).not.to.have.property(field);
                if (event.probe)
                    expect(Object.keys(event.probe).sort()).to.deep.equal(['id', 'kind']);
            }
            for (const field of ['schemaVersion', 'sessionId', 'legs', 'aggregateUpload', 'combinedWorkload'])
                expect(result).not.to.have.property(field);
            for (const projectedOutput of result.outputs) {
                for (const field of ['legId', 'display', 'outputKind', 'destinations', 'limits', 'recommendation'])
                    expect(projectedOutput).not.to.have.property(field);
                for (const video of projectedOutput.videos)
                    expect(Object.keys(video).sort()).to.deep.equal(['display', 'fpsDen', 'fpsNum', 'height', 'width']);
                expect(projectedOutput.measurement).not.to.have.property('probes');
                for (const evidence of projectedOutput.measurement.evidence || [])
                    expect(Object.keys(evidence).sort()).to.deep.equal(['method', 'platform', 'success']);
            }
            return { events, result };
        } finally {
            clearTimeout(timeout!);
        }
    }

    it('exposes only the run-based Auto Optimizer API', function() {
        expect(autoConfig.run).to.be.a('function');
        expect(Object.keys(autoConfig)).to.deep.equal(['run']);
        for (const rawMethod of [
            '__AutoConfigNative',
            'GetAutoConfigCapabilities',
            'CreateAutoConfigSession',
            'StartAutoConfigSession',
            'ConfirmAutoConfigProbeIngest',
            'GetAutoConfigResult',
            'CancelAutoConfigSession',
            'CloseAutoConfigSession',
        ]) {
            expect((osn.NodeObs as any)[rawMethod]).to.equal(undefined);
            expect((osn.NodeObs.AutoConfig as any)[rawMethod]).to.equal(undefined);
        }
    });

    it('accepts registered Enhanced Broadcasting canvas identities 0 and 1', async function() {
        const verticalCanvas = osn.VideoFactory.create();
        let nativeRun: AutoConfigRun | null = null;
        try {
            expect(obs.defaultVideoContext.canvasId).to.equal(0);
            expect(verticalCanvas.canvasId).to.equal(1);
            nativeRun = autoConfig.run(
                pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId),
                () => undefined,
            );
            await nativeRun.cancel();
            expect((await nativeRun.result).status).to.equal('cancelled');
        } finally {
            if (nativeRun)
                await nativeRun.cancel().catch(() => undefined);
            verticalCanvas.destroy();
        }
    });

    it('rejects Twitch-managed outputs outside Enhanced Broadcasting stream setups', function() {
        const direct = {
            streamSetup: 'direct-single',
            outputs: [output({ outputKind: 'twitch-enhanced-broadcasting' })],
        } as IAutoConfigRequest;
        expect(() => autoConfig.run(direct, () => undefined)).to.throw('invalid_autoconfig_output_kind');

        const enhanced = {
            streamSetup: 'enhanced-broadcasting',
            outputs: [output({
                outputKind: 'standard',
                destinations: ['twitch'],
            })],
        } as IAutoConfigRequest;
        expect(() => autoConfig.run(enhanced, () => undefined)).to.throw('invalid_autoconfig_output_kind');
    });

    it('accepts the exact Enhanced Broadcasting plus two companion output stream setup', async function() {
        const verticalCanvas = osn.VideoFactory.create();
        let nativeRun: AutoConfigRun | null = null;
        try {
            nativeRun = autoConfig.run(
                enhancedBroadcastingDualOutputRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId),
                () => undefined,
            );
            await nativeRun.cancel();
            expect((await nativeRun.result).status).to.equal('cancelled');
        } finally {
            if (nativeRun)
                await nativeRun.cancel().catch(() => undefined);
            verticalCanvas.destroy();
        }
    });

    it('rejects unsafe or inexact Enhanced Broadcasting companion outputs', function() {
        const verticalCanvas = osn.VideoFactory.create();
        try {
            const wrongCanvas = enhancedBroadcastingDualOutputRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId);
            wrongCanvas.outputs[1].current.canvasId = verticalCanvas.canvasId;
            expect(() => autoConfig.run(wrongCanvas, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_dual_output');

            const custom = enhancedBroadcastingDualOutputRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId);
            custom.outputs[1].destinations = ['custom'];
            expect(() => autoConfig.run(custom, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_dual_output');

            const providerOwnedCompanion = enhancedBroadcastingDualOutputRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId);
            providerOwnedCompanion.outputs[1].outputKind = 'twitch-enhanced-broadcasting';
            expect(() => autoConfig.run(providerOwnedCompanion, () => undefined))
                .to.throw('invalid_autoconfig_enhanced_broadcasting_dual_output');

            const implicitOwnership = enhancedBroadcastingDualOutputRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            delete implicitOwnership.outputs[1].outputKind;
            expect(() => autoConfig.run(implicitOwnership, () => undefined)).to.throw('Invalid Auto Optimizer output');
        } finally {
            verticalCanvas.destroy();
        }
    });

    it('allows estimate-only requests to omit a canvas identity', async function() {
        const request = {
            streamSetup: 'custom-rtmp',
            outputs: [output()],
        } as any;
        delete request.outputs[0].current.canvasId;
        const nativeRun = autoConfig.run(request, () => undefined);
        await nativeRun.cancel();
        expect((await nativeRun.result).status).to.equal('cancelled');
    });

    it('rejects missing, negative, equal, and unknown Enhanced Broadcasting canvas identities', function() {
        const verticalCanvas = osn.VideoFactory.create();
        try {
            const missingPrimary = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            delete missingPrimary.outputs[0].current.canvasId;
            expect(() => autoConfig.run(missingPrimary, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const negativePrimary = pairedEnhancedBroadcastingRequest(-1, verticalCanvas.canvasId);
            expect(() => autoConfig.run(negativePrimary, () => undefined)).to.throw('Invalid Auto Optimizer current settings');

            const missingAdditional = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            delete missingAdditional.outputs[0].additionalVideo.current.canvasId;
            expect(() => autoConfig.run(missingAdditional, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const negativeAdditional = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, -1);
            expect(() => autoConfig.run(negativeAdditional, () => undefined)).to.throw('Invalid Auto Optimizer additional video');

            const fractionalPrimary = pairedEnhancedBroadcastingRequest(0.5, verticalCanvas.canvasId);
            expect(() => autoConfig.run(fractionalPrimary, () => undefined)).to.throw('Invalid Auto Optimizer current settings');

            const stringPrimary = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            stringPrimary.outputs[0].current.canvasId = '0';
            expect(() => autoConfig.run(stringPrimary, () => undefined)).to.throw('Invalid Auto Optimizer current settings');

            const nullPrimary = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            nullPrimary.outputs[0].current.canvasId = null;
            expect(() => autoConfig.run(nullPrimary, () => undefined)).to.throw('Invalid Auto Optimizer current settings');

            const unsafeIntegerPrimary = pairedEnhancedBroadcastingRequest(Number.MAX_SAFE_INTEGER + 1, verticalCanvas.canvasId);
            expect(() => autoConfig.run(unsafeIntegerPrimary, () => undefined)).to.throw('Invalid Auto Optimizer current settings');

            const equalCanvasIds = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, obs.defaultVideoContext.canvasId);
            expect(() => autoConfig.run(equalCanvasIds, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const unknownPrimary = pairedEnhancedBroadcastingRequest(999999, verticalCanvas.canvasId);
            expect(() => autoConfig.run(unknownPrimary, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const unknownAdditional = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, 999999);
            expect(() => autoConfig.run(unknownAdditional, () => undefined)).to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');
        } finally {
            verticalCanvas.destroy();
        }
    });

    it('never dials an ineligible custom RTMP active-probe request', async function() {
        const mock = await startConnectionSink(mockPort);
        const secret = 'must-not-appear-in-result';
        try {
            const response = await run({
                streamSetup: 'custom-rtmp',
                outputs: [output({
                    limits: { maxWidth: 1920, maxHeight: 1080, maxFpsNum: 60, maxFpsDen: 1 },
                    probes: [{
                        id: 'twitch-primary',
                        kind: 'twitch-standard',
                        server: `rtmp://127.0.0.1:${mockPort}/live`,
                        streamKey: secret,
                    }],
                })],
            });

            expect(mock.getConnections()).to.equal(0);
            expect(mock.getBytes()).to.equal(0);
            expect(response.result.status).to.equal('complete');
            expect(response.result.outputs[0].measurement.mode).to.equal('estimated');
            expect(response.result.outputs[0].measurement.reason).to.equal('custom_rtmp');
            expect(response.result.outputs[0].encoding!.bitrateKbps).to.equal(2500);
            expect(JSON.stringify(response.result)).not.to.contain(secret);
            expect(JSON.stringify(response.events)).not.to.contain(secret);
            expect(response.events.some(event => event.code === 'active_probe_not_eligible')).to.equal(true);
            expect(response.events.every((event, index) => index === 0 || event.progress >= response.events[index - 1].progress)).to.equal(true);
            const hardwareAttempt = response.events.find(event => event.code === 'hardware_testing_encoder' ||
                event.code === 'hardware_testing_encoder_surfaces' ||
                event.code === 'hardware_testing_x264');
            expect(hardwareAttempt).not.to.equal(undefined);
            if (!hardwareAttempt)
                throw new Error('Expected a hardware attempt event');
            expect(hardwareAttempt.encoderId).to.be.a('string').and.not.equal('');
            expect(hardwareAttempt.encoderFamily).to.be.a('string').and.not.equal('');
            expect(hardwareAttempt.encoderTitle).to.be.a('string').and.not.equal('');
            expect(hardwareAttempt.width).to.be.greaterThan(0);
            expect(hardwareAttempt.height).to.be.greaterThan(0);
            expect(hardwareAttempt.fpsNum).to.be.greaterThan(0);
            expect(hardwareAttempt.fpsDen).to.be.greaterThan(0);
            expect(response.events.some(event => (event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_x264' ||
                event.code === 'hardware_validating_target_cadence') &&
                event.width === 1920 && event.height === 1080 && event.fpsNum === 60 && event.fpsDen === 1))
                .to.equal(true);
            const qualityInput = response.events.find(event => event.code === 'recommendation_selecting_quality');
            if (!qualityInput)
                throw new Error('Expected a quality-selection input event');
            expect(qualityInput.availableBitrateKbps).to.equal(2500);
            // This estimate-only path may test the higher canvas ceiling, but
            // cannot promote without a successful provider bandwidth probe.
            expect(qualityInput.width).to.equal(1280);
            const qualityResult = response.events.find(event => event.code === 'recommendation_quality_selected');
            if (!qualityResult)
                throw new Error('Expected a quality-selection result event');
            expect(qualityResult.selectedBitrateKbps).to.equal(2500);
            expect(response.result.outputs[0].videos[0].width).to.be.at.most(1280);
            expect(response.result.outputs[0].videos[0].height).to.be.at.most(720);
            expect(response.result.outputs[0].videos[0].fpsNum).to.equal(30);
            expect(response.result.outputs[0].videos[0].fpsDen).to.equal(1);
            expect(response.result.outputs[0].encoding!.encoderFamily).to.be.a('string').and.not.equal('');
            expect(response.result.outputs[0].encoding!.encoderTitle).to.be.a('string').and.not.equal('');
            const testedPresets: Record<string, string> = {
                obs_nvenc_h264_tex: 'p5',
                obs_qsv11_v2: 'TU4',
                h264_texture_amf: 'quality',
                'com.apple.videotoolbox.videoencoder.h264.gva': 'high',
                'com.apple.videotoolbox.videoencoder.ave.avc': 'high',
                obs_x264: 'veryfast',
            };
            const recommendation = response.result.outputs[0].encoding!;
            const locallyExpectedEncoder = process.env.AUTOCONFIG_EXPECT_ENCODER;
            if (locallyExpectedEncoder)
                expect(recommendation.encoderId).to.equal(locallyExpectedEncoder);
            expect(recommendation.preset).to.equal(testedPresets[recommendation.encoderId]);
            const expectedTitles: Record<string, string> = {
                obs_nvenc_h264_tex: 'NVIDIA NVENC H.264 (new)',
                obs_qsv11_v2: 'QuickSync H.264',
                h264_texture_amf: 'AMD HW H.264',
                'com.apple.videotoolbox.videoencoder.h264.gva': 'Apple VT H264 Hardware Encoder',
                'com.apple.videotoolbox.videoencoder.ave.avc': 'Apple VT H264 Hardware Encoder',
                obs_x264: 'Software (x264)',
            };
            expect(recommendation.encoderTitle).to.equal(expectedTitles[recommendation.encoderId]);
        } finally {
            await mock.close();
        }
    });

    it('default-denies non-official YouTube probe endpoints without dialing them', async function() {
        const mock = await startConnectionSink(mockPort);
        const secret = 'youtube-secret-must-not-appear';
        try {
            const response = await run({
                streamSetup: 'direct-single',
                outputs: [output({
                    destinations: ['youtube'],
                    probes: [{
                        id: 'youtube-primary',
                        kind: 'youtube-unbound',
                        server: `rtmps://127.0.0.1:${mockPort}/live2`,
                        streamKey: secret,
                    }],
                })],
            });

            expect(mock.getConnections()).to.equal(0);
            expect(mock.getBytes()).to.equal(0);
            expect(response.result.outputs[0].measurement.mode).to.equal('estimated');
            expect(JSON.stringify(response.result)).not.to.contain(secret);
            expect(JSON.stringify(response.events)).not.to.contain(secret);
            expect(response.events.some(event => event.code === 'active_probe_not_eligible')).to.equal(true);
        } finally {
            await mock.close();
        }
    });

    it('keeps a shared cloud output estimate-only when no supplied probe is eligible', async function() {
        const response = await run({
            streamSetup: 'cloud-multistream',
            outputs: [output({
                destinations: ['twitch', 'youtube'],
                estimateReason: 'cloud_multistream',
                probes: [{
                    id: 'cloud-twitch-only',
                    kind: 'twitch-standard',
                    // Individual endpoint validation must reject this credential
                    // without affecting the partial-provider coverage policy.
                    server: `rtmp://127.0.0.1:${mockPort}/live`,
                    streamKey: 'incomplete-cloud-secret',
                }],
            })],
        });
        expect(response.result.outputs[0].measurement.mode).to.equal('estimated');
        expect(response.result.outputs[0].measurement.reason).to.equal('cloud_multistream');
        expect(response.events.some(event => event.code === 'twitch_probe_started')).to.equal(false);
        expect(JSON.stringify(response.result)).not.to.contain('incomplete-cloud-secret');
        expect(JSON.stringify(response.events)).not.to.contain('incomplete-cloud-secret');
    });

    it('default-denies dual-output active probes whose two outputs reuse one canvas identity', async function() {
        const twitchSecret = 'dual-twitch-secret';
        const youtubeSecret = 'dual-youtube-secret';
        const response = await run({
            streamSetup: 'dual-output',
            outputs: [
                output({
                    outputId: 'horizontal',
                    display: 'horizontal',
                    destinations: ['twitch'],
                    current: { ...output().current, bitrateKbps: 6000 },
                    estimateReason: 'dual_output',
                    probes: [{
                        id: 'dual-twitch',
                        kind: 'twitch-standard',
                        server: 'rtmp://live.twitch.tv/app',
                        streamKey: twitchSecret,
                    }],
                }),
                output({
                    outputId: 'vertical',
                    display: 'vertical',
                    destinations: ['youtube'],
                    current: { ...output().current, bitrateKbps: 6000 },
                    estimateReason: 'dual_output',
                    probes: [{
                        id: 'dual-youtube',
                        kind: 'youtube-unbound',
                        server: 'rtmps://a.rtmps.youtube.com/live2',
                        streamKey: youtubeSecret,
                    }],
                }),
            ],
        });

        expect(response.result.outputs).to.have.length(2);
        expect(response.result.outputs.every(resultOutput => resultOutput.measurement.mode === 'estimated')).to.equal(true);
        expect(response.result.outputs.every(resultOutput => resultOutput.measurement.reason === 'dual_output')).to.equal(true);
        expect(response.events.some(event => event.code === 'dual_output_testing_workload')).to.equal(false);
        expect(response.events.some(event => event.code === 'dual_output_allocating_upload')).to.equal(false);
        expect(response.events.filter(event => event.code === 'dual_output_multiple_active_legs')).to.have.length(2);
        expect(response.events.some(event => event.code === 'twitch_probe_started' || event.code === 'youtube_probe_started')).to.equal(false);
        expect(new Set(response.result.outputs.map(resultOutput => resultOutput.encoding!.encoderId)).size).to.equal(1);
        expect(JSON.stringify(response.result)).not.to.contain(twitchSecret);
        expect(JSON.stringify(response.result)).not.to.contain(youtubeSecret);
        expect(JSON.stringify(response.events)).not.to.contain(twitchSecret);
        expect(JSON.stringify(response.events)).not.to.contain(youtubeSecret);
    });

    it('clamps estimate-only results to bundled platform caps without raising current bitrate', async function() {
        const twitch = await run({
            streamSetup: 'direct-single',
            outputs: [output({
                destinations: ['twitch'],
                current: { ...output().current, bitrateKbps: 8000 },
                estimateReason: 'probe_disabled',
            })],
        });
        expect(twitch.result.outputs[0].measurement.mode).to.equal('estimated');
        expect(twitch.result.outputs[0].encoding!.bitrateKbps).to.equal(6000);

        const alreadyConservative = await run({
            streamSetup: 'direct-single',
            outputs: [output({
                destinations: ['twitch'],
                current: { ...output().current, bitrateKbps: 2500 },
                estimateReason: 'probe_disabled',
            })],
        });
        expect(alreadyConservative.result.outputs[0].encoding!.bitrateKbps).to.equal(2500);

        const globallyCapped = await run({
            streamSetup: 'direct-single',
            outputs: [output({
                destinations: ['other'],
                current: { ...output().current, bitrateKbps: 12000 },
                estimateReason: 'probe_disabled',
            })],
        });
        expect(globallyCapped.result.outputs[0].measurement.mode).to.equal('estimated');
        expect(globallyCapped.result.outputs[0].encoding!.bitrateKbps).to.equal(8000);
    });

    it('uses the strictest destination/request cap and replaces an unavailable encoder', async function() {
        const response = await run({
            streamSetup: 'cloud-multistream',
            outputs: [output({
                destinations: ['youtube', 'twitch'],
                current: {
                    ...output().current,
                    bitrateKbps: 9000,
                    encoderId: 'definitely-not-a-real-encoder',
                },
                limits: { maxBitrateKbps: 1800 },
                estimateReason: 'cloud_multistream',
            })],
        });

        expect(response.result.outputs[0].encoding!.bitrateKbps).to.equal(1800);
        expect(response.result.outputs[0].encoding!.encoderId).not.to.equal('definitely-not-a-real-encoder');
        expect(response.result.outputs[0].encoding!.codec).to.equal('h264');
        expect(['obs_nvenc_h264_tex', 'qsv', 'amd', 'apple', 'x264']).to.include(response.result.outputs[0].encoding!.encoderFamily);
        expect(response.result.outputs[0].measurement.confidence).to.equal('medium');
    });

    it('does not return Twitch-managed encoding settings for a paired Enhanced Broadcasting output', async function() {
        const response = await run({
            streamSetup: 'enhanced-broadcasting',
            outputs: [output({
                display: 'both',
                outputKind: 'twitch-enhanced-broadcasting',
                destinations: ['twitch'],
                current: {
                    ...output().current,
                    encoderId: 'obs_nvenc_av1_tex',
                },
                additionalVideo: {
                    display: 'vertical',
                    current: {
                        ...output().current,
                        width: 720,
                        height: 1280,
                        encoderId: 'obs_nvenc_av1_tex',
                    },
                },
                estimateReason: 'enhanced_broadcasting',
            })],
        });

        expect(response.events.some(event => event.code === 'hardware_provider_managed')).to.equal(true);
        expect(response.events.some(event => event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_encoder_surfaces' ||
            event.code === 'hardware_validating_target_cadence' || event.code === 'hardware_testing_x264'))
            .to.equal(false);
        expect(response.events.some(event => event.code === 'recommendation_provider_managed')).to.equal(true);
        expect(response.result.outputs[0].encoding).to.equal(undefined);
        expect(response.result.outputs[0].videos.map(video => video.display)).to.deep.equal(['horizontal', 'vertical']);
    });

    it('preserves a paired vertical recommendation when Enhanced Broadcasting is estimate-only', async function() {
        const response = await run({
            streamSetup: 'enhanced-broadcasting',
            outputs: [output({
                display: 'both',
                outputKind: 'twitch-enhanced-broadcasting',
                destinations: ['twitch'],
                estimateReason: 'enhanced_broadcasting',
                additionalVideo: {
                    display: 'vertical',
                    current: {
                        ...output().current,
                        width: 720,
                        height: 1280,
                        fpsNum: 60,
                    },
                    limits: {
                        maxWidth: 1080,
                        maxHeight: 1920,
                        maxFpsNum: 60,
                        maxFpsDen: 1,
                    },
                },
            })],
        });

        expect(response.result.outputs[0].measurement.mode).to.equal('estimated');
        expect(response.result.outputs[0].videos.find(video => video.display === 'vertical')).to.deep.equal({
            display: 'vertical',
            width: 720,
            height: 1280,
            fpsNum: 60,
            fpsDen: 1,
        });
        expect(response.events.some(event => event.code === 'recommendation_provider_managed')).to.equal(true);
    });

    it('cancels a newly started run and makes cleanup observable before returning', async function() {
        const nativeRun = autoConfig.run({
            streamSetup: 'custom-rtmp',
            outputs: [output()],
        } as IAutoConfigRequest,
            () => undefined);

        await nativeRun.cancel();
        const result = await nativeRun.result;
        expect(result.status).to.equal('cancelled');
        expect(result.error.code).to.equal('cancelled');
    });

    it('cancels a running session only after its benchmark worker has cleaned up', async function() {
        const nativeRun = autoConfig.run({
            streamSetup: 'custom-rtmp',
            outputs: [output()],
        } as IAutoConfigRequest,
            () => undefined);
        // Wait for hardware testing to start so this cancels active benchmark
        // work rather than a session that has not begun.
        await new Promise(resolve => setTimeout(resolve, 100));

        await nativeRun.cancel();
        const result = await nativeRun.result;
        expect(result.status).to.equal('cancelled');
        expect(result.error.code).to.equal('cancelled');
    });

    it('accepts additional unprobed destinations and cancels both concurrent Dual Output hardware workloads before returning', async function() {
        const verticalCanvas = osn.VideoFactory.create();
        let nativeRun: AutoConfigRun | null = null;
        try {
            const started = new Promise<void>((resolve, reject) => {
                const eventCodes: string[] = [];
                const timeout = setTimeout(
                    () => reject(new Error(`Timed out waiting for the concurrent Dual Output workload; events=${eventCodes.join(',')}`)),
                    15000);
                nativeRun = autoConfig.run(
                    {
                        streamSetup: 'dual-output',
                        outputs: [
                            output({
                                outputId: 'horizontal',
                                display: 'horizontal',
                                destinations: ['twitch', 'kick'],
                                probes: [{
                                    id: 'dual-cancel-twitch',
                                    kind: 'twitch-standard',
                                    server: 'rtmp://live.twitch.tv/app',
                                    streamKey: 'integration-test-twitch-key',
                                }],
                            }),
                            output({
                                outputId: 'vertical',
                                display: 'vertical',
                                destinations: ['youtube'],
                                current: {
                                    ...output().current,
                                    canvasId: verticalCanvas.canvasId,
                                    width: 720,
                                    height: 1280,
                                },
                                probes: [{
                                    id: 'dual-cancel-youtube',
                                    kind: 'youtube-unbound',
                                    server: 'rtmps://a.rtmps.youtube.com/live2',
                                    streamKey: 'integration-test-youtube-key',
                                }],
                            }),
                        ],
                    } as IAutoConfigRequest,
                    event => {
                        if (event.code)
                            eventCodes.push(event.code);
                        if (event.code === 'dual_output_testing_workload') {
                            clearTimeout(timeout);
                            resolve();
                        } else if (event.type === 'complete' || event.type === 'cancelled') {
                            clearTimeout(timeout);
                            reject(new Error(`Auto Optimizer stopped before the concurrent Dual Output workload; events=${eventCodes.join(',')}`));
                        }
                    });
            });
            await started;
            await new Promise(resolve => setTimeout(resolve, 100));
            await nativeRun!.cancel();
            const result = await nativeRun!.result;
            expect(result.status).to.equal('cancelled');
            expect(result.error.code).to.equal('cancelled');
            nativeRun = null;
        } finally {
            if (nativeRun)
                await nativeRun.cancel().catch(() => undefined);
            verticalCanvas.destroy();
        }
    });

    it('cancels an active session before OBS_service resets its video context', async function() {
        const nativeRun = await startSessionAtHardwareAttempt();

        try {
            expect(() => osn.NodeObs.OBS_service_resetVideoContext()).not.to.throw();

            const result = await nativeRun.result;
            expect(result.status).to.equal('cancelled');
            expect(result.error.code).to.equal('cancelled');
        } finally {
            await nativeRun.cancel().catch(() => undefined);
        }
    });

    it('cancels an active session before Advanced settings reset video', async function() {
        const advancedSettings = obs.getSettingsContainer('Advanced');
        const nativeRun = await startSessionAtHardwareAttempt();

        try {
            expect(() => obs.setSettingsContainer('Advanced', advancedSettings)).not.to.throw();

            const result = await nativeRun.result;
            expect(result.status).to.equal('cancelled');
            expect(result.error.code).to.equal('cancelled');
        } finally {
            await nativeRun.cancel().catch(() => undefined);
        }
    });

    it('rejects malformed public requests before creating a session', function() {
        const expectInvalid =
            (request: unknown,
                error: string) => { expect(() => autoConfig.run(request as IAutoConfigRequest, () => undefined)).to.throw(error); };

        expectInvalid({
            streamSetup: 'not-a-stream-setup',
            outputs: [output()],
        },
            'Invalid Auto Optimizer request');

        expectInvalid({
            streamSetup: 'direct-single',
            outputs: [output({ limits: { maxWidth: 1920 } })],
        },
            'Invalid Auto Optimizer limits');

        expectInvalid({
            streamSetup: 'direct-single',
            outputs: [output({ limits: { maxWidth: 4294969216, maxHeight: 4294968376 } })],
        },
            'Invalid Auto Optimizer limits');

        expectInvalid({
            streamSetup: 'direct-single',
            outputs: [output({ limits: { maxFpsNum: 60, maxFpsDen: -1 } })],
        },
            'Invalid Auto Optimizer limits');

        expectInvalid({
            streamSetup: 'direct-single',
            outputs: [output({ limits: { maxFpsDen: 1 } })],
        },
            'Invalid Auto Optimizer limits');

        expectInvalid({
            streamSetup: 'enhanced-broadcasting',
            outputs: [output({
                display: 'horizontal',
                outputKind: 'twitch-enhanced-broadcasting',
                additionalVideo: {
                    display: 'vertical',
                    current: { ...output().current, width: 720, height: 1280 },
                },
            })],
        },
            'Invalid Auto Optimizer additional video');

        expectInvalid({
            streamSetup: 'enhanced-broadcasting',
            outputs: [output({
                display: 'both',
                outputKind: 'twitch-enhanced-broadcasting',
                additionalVideo: {
                    display: 'vertical',
                    current: { ...output().current, width: 720, height: 1280 },
                    limits: { maxWidth: 1080 },
                },
            })],
        },
            'Invalid Auto Optimizer additional video');

        expectInvalid({
            streamSetup: 'dual-output',
            outputs: [
                output({ outputId: 'duplicate' }),
                output({ outputId: 'duplicate', display: 'vertical' }),
            ],
        },
            'Duplicate Auto Optimizer outputId');

        expectInvalid({
            streamSetup: 'dual-output',
            outputs: [
                output({
                    outputId: 'horizontal',
                    probes: [{
                        id: 'duplicate-probe',
                        kind: 'twitch-standard',
                        server: 'rtmp://live.twitch.tv/app',
                        streamKey: 'integration-test-key',
                    }],
                }),
                output({
                    outputId: 'vertical',
                    display: 'vertical',
                    probes: [{
                        id: 'duplicate-probe',
                        kind: 'youtube-unbound',
                        server: 'rtmps://a.rtmps.youtube.com/live2',
                        streamKey: 'integration-test-key',
                    }],
                }),
            ],
        },
            'Invalid Auto Optimizer probe identity');
    });

    it('rejects more outputs than Desktop can create', function() {
        expect(() => autoConfig.run({
            streamSetup: 'dual-output',
            outputs: [
                output({ outputId: 'horizontal', display: 'horizontal' }),
                output({ outputId: 'vertical', display: 'vertical' }),
                output({ outputId: 'unexpected-third', display: 'horizontal' }),
            ],
        } as IAutoConfigRequest,
            () => undefined))
            .to.throw('Invalid Auto Optimizer outputs');
    });

    it('cancels an active session before removing its video context', async function() {
        const nativeRun = autoConfig.run({
            streamSetup: 'custom-rtmp',
            outputs: [output()],
        } as IAutoConfigRequest,
            () => undefined);
        await new Promise(resolve => setTimeout(resolve, 100));

        const startedAt = Date.now();
        obs.destroyDefaultVideoContext();
        expect(Date.now() - startedAt).to.be.lessThan(15000);

        const result = await nativeRun.result;
        expect(result.status).to.equal('cancelled');
        expect(result.error.code).to.equal('cancelled');
        obs.createDefaultVideoContext();
    });

    it('disconnects cleanly while a session worker is running', async function() {
        autoConfig.run({
            streamSetup: 'custom-rtmp',
            outputs: [output()],
        } as IAutoConfigRequest,
            () => undefined);
        await new Promise(resolve => setTimeout(resolve, 100));

        const startedAt = Date.now();
        obs.shutdown();
        obs = null;
        expect(Date.now() - startedAt).to.be.lessThan(15000);
    });
});
