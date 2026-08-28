import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import type {
    IAutoConfigCapabilities,
    IAutoConfigEvent,
    IAutoConfigLegRequest,
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

    function leg(overrides: Partial<IAutoConfigLegRequest> = {}): IAutoConfigLegRequest {
        return {
            legId: 'primary',
            display: 'horizontal',
            destinations: [{ platform: 'custom' }],
            current: {
                canvasId: obs.defaultVideoContext.canvasId,
                width: 1280,
                height: 720,
                fpsNum: 30,
                fpsDen: 1,
                bitrateKbps: 2500,
                encoderId: 'obs_x264',
                codec: 'h264',
                preset: 'veryfast',
            },
            ...overrides,
        };
    }

    function pairedEnhancedBroadcastingRequest(primaryCanvasId: number, additionalCanvasId: number): IAutoConfigRequest {
        return {
            schemaVersion: 1,
            topology: 'enhanced-broadcasting',
            legs: [leg({
                display: 'both',
                destinations: [{ platform: 'twitch' }],
                current: { ...leg().current, canvasId: primaryCanvasId },
                additionalVideo: {
                    display: 'vertical',
                    current: {
                        ...leg().current,
                        canvasId: additionalCanvasId,
                        width: 720,
                        height: 1280,
                        fpsNum: 60,
                    },
                },
            })],
            activeProbes: [{
                probeId: 'enhanced-broadcasting-primary',
                kind: 'twitch-enhanced-broadcasting',
                legId: 'primary',
                serviceName: 'Twitch',
                server: 'auto',
                streamKey: 'integration-test-key',
            }],
        };
    }

    async function startSessionAtHardwareAttempt(): Promise<string> {
        let sessionId = '';
        let timeout: ReturnType<typeof setTimeout>;
        const hardwareAttemptStarted = new Promise<void>((resolve, reject) => {
            timeout = setTimeout(() => reject(new Error('Timed out waiting for the Auto Optimizer scratch workload')), 15000);
            const onEvent = (event: IAutoConfigEvent) => {
                if (event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_encoder_surfaces' ||
                    event.code === 'hardware_testing_x264') {
                    clearTimeout(timeout);
                    resolve();
                } else if (event.type === 'complete' || event.type === 'cancelled') {
                    clearTimeout(timeout);
                    reject(new Error(`Auto Optimizer stopped before starting a scratch workload: ${event.code}`));
                }
            };

            sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
                schemaVersion: 1,
                topology: 'custom-rtmp',
                legs: [leg()],
            } as IAutoConfigRequest), onEvent);
            osn.NodeObs.StartAutoConfigSession(sessionId);
        });

        try {
            await hardwareAttemptStarted;
            return sessionId;
        } catch (error) {
            if (sessionId) {
                try { osn.NodeObs.CancelAutoConfigSession(sessionId); } catch (_) { /* best effort */ }
                try { osn.NodeObs.CloseAutoConfigSession(sessionId); } catch (_) { /* best effort */ }
            }
            throw error;
        }
    }

    async function run(request: IAutoConfigRequest): Promise<{
        sessionId: string;
        events: IAutoConfigEvent[];
        result: IAutoConfigResult;
    }> {
        const events: IAutoConfigEvent[] = [];
        let sessionId = '';

        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                if (sessionId) {
                    try { osn.NodeObs.CancelAutoConfigSession(sessionId); } catch (_) { /* best effort */ }
                    try { osn.NodeObs.CloseAutoConfigSession(sessionId); } catch (_) { /* best effort */ }
                }
                reject(new Error('Auto Optimizer session timed out'));
            }, 330000);

            const onEvent = (event: IAutoConfigEvent) => {
                events.push(event);
                if (event.type !== 'complete' && event.type !== 'cancelled') return;

                try {
                    const raw = osn.NodeObs.GetAutoConfigResult(sessionId);
                    const result = JSON.parse(raw) as IAutoConfigResult;
                    osn.NodeObs.CloseAutoConfigSession(sessionId);
                    clearTimeout(timeout);
                    resolve({ sessionId, events, result });
                } catch (error) {
                    clearTimeout(timeout);
                    reject(error);
                }
            };

            try {
                sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify(request), onEvent);
                osn.NodeObs.StartAutoConfigSession(sessionId);
            } catch (error) {
                clearTimeout(timeout);
                reject(error);
            }
        });
    }

    it('advertises the versioned, Desktop-owned apply contract', function() {
        expect(osn.NodeObs.ConfirmAutoConfigProbeIngest).to.be.a('function');
        const capabilities = JSON.parse(osn.NodeObs.GetAutoConfigCapabilities()) as IAutoConfigCapabilities;
        expect(capabilities).to.deep.equal({
            apiVersion: 2,
            resultSchemaVersion: 1,
            previewApplySplit: true,
            awaitableCancel: true,
            perUploadLegResults: true,
            desktopOwnedApply: true,
            multipleActiveProbes: true,
            bandwidthModes: ['twitch-standard-active', 'twitch-enhanced-broadcasting-active', 'youtube-unbound-active', 'estimate'],
        });
    });

    it('accepts registered Enhanced Broadcasting canvas identities 0 and 1', function() {
        const verticalCanvas = osn.VideoFactory.create();
        let sessionId = '';
        try {
            expect(obs.defaultVideoContext.canvasId).to.equal(0);
            expect(verticalCanvas.canvasId).to.equal(1);
            sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify(
                pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId)), () => undefined);
            expect(sessionId).to.be.a('string').and.not.equal('');
        } finally {
            if (sessionId) osn.NodeObs.CloseAutoConfigSession(sessionId);
            verticalCanvas.destroy();
        }
    });

    it('allows estimate-only requests to omit a canvas identity', function() {
        const request = {
            schemaVersion: 1,
            topology: 'custom-rtmp',
            legs: [leg()],
        } as any;
        delete request.legs[0].current.canvasId;
        const sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify(request), () => undefined);
        expect(sessionId).to.be.a('string').and.not.equal('');
        osn.NodeObs.CloseAutoConfigSession(sessionId);
    });

    it('rejects missing, negative, equal, and unknown Enhanced Broadcasting canvas identities', function() {
        const verticalCanvas = osn.VideoFactory.create();
        try {
            const missingPrimary = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            delete missingPrimary.legs[0].current.canvasId;
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(missingPrimary), () => undefined))
                .to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const negativePrimary = pairedEnhancedBroadcastingRequest(-1, verticalCanvas.canvasId);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(negativePrimary), () => undefined))
                .to.throw('invalid_autoconfig_current_settings');

            const missingAdditional = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            delete missingAdditional.legs[0].additionalVideo.current.canvasId;
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(missingAdditional), () => undefined))
                .to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const negativeAdditional = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, -1);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(negativeAdditional), () => undefined))
                .to.throw('invalid_autoconfig_additional_video');

            const fractionalPrimary = pairedEnhancedBroadcastingRequest(0.5, verticalCanvas.canvasId);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(fractionalPrimary), () => undefined))
                .to.throw('invalid_autoconfig_current_settings');

            const stringPrimary = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            stringPrimary.legs[0].current.canvasId = '0';
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(stringPrimary), () => undefined))
                .to.throw('invalid_autoconfig_current_settings');

            const nullPrimary = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, verticalCanvas.canvasId) as any;
            nullPrimary.legs[0].current.canvasId = null;
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(nullPrimary), () => undefined))
                .to.throw('invalid_autoconfig_current_settings');

            const unsafeIntegerPrimary = pairedEnhancedBroadcastingRequest(Number.MAX_SAFE_INTEGER + 1, verticalCanvas.canvasId);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(unsafeIntegerPrimary), () => undefined))
                .to.throw('invalid_autoconfig_current_settings');

            const equalCanvasIds = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, obs.defaultVideoContext.canvasId);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(equalCanvasIds), () => undefined))
                .to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const unknownPrimary = pairedEnhancedBroadcastingRequest(999999, verticalCanvas.canvasId);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(unknownPrimary), () => undefined))
                .to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');

            const unknownAdditional = pairedEnhancedBroadcastingRequest(obs.defaultVideoContext.canvasId, 999999);
            expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify(unknownAdditional), () => undefined))
                .to.throw('invalid_autoconfig_enhanced_broadcasting_canvas');
        } finally {
            verticalCanvas.destroy();
        }
    });

    it('never dials an ineligible custom RTMP active-probe request', async function() {
        const mock = await startConnectionSink(mockPort);
        const secret = 'must-not-appear-in-result';
        try {
            const response = await run({
                schemaVersion: 1,
                topology: 'custom-rtmp',
                legs: [leg({ limits: { maxWidth: 1920, maxHeight: 1080, maxFpsNum: 60, maxFpsDen: 1 } })],
                activeProbes: [{
                    probeId: 'twitch-primary',
                    kind: 'twitch-standard',
                    legId: 'primary',
                    serviceName: 'Twitch',
                    server: `rtmp://127.0.0.1:${mockPort}/live`,
                    streamKey: secret,
                }],
            });

            expect(mock.getConnections()).to.equal(0);
            expect(mock.getBytes()).to.equal(0);
            expect(response.result.status).to.equal('complete');
            expect(response.result.legs[0].measurement.mode).to.equal('estimated');
            expect(response.result.legs[0].measurement.reason).to.equal('custom_rtmp');
            expect(response.result.legs[0].recommendation.bitrateKbps).to.equal(2500);
            expect(JSON.stringify(response.result)).not.to.contain(secret);
            expect(JSON.stringify(response.events)).not.to.contain(secret);
            expect(response.events.some(event => event.code === 'active_probe_not_eligible')).to.equal(true);
            expect(response.events.every((event, index) => index === 0 || event.progress >= response.events[index - 1].progress)).to.equal(true);
            const hardwareAttempt = response.events.find(event =>
                event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_encoder_surfaces' ||
                event.code === 'hardware_testing_x264');
            expect(hardwareAttempt).not.to.equal(undefined);
            if (!hardwareAttempt) throw new Error('Expected a hardware attempt event');
            expect(hardwareAttempt.encoderId).to.be.a('string').and.not.equal('');
            expect(hardwareAttempt.encoderFamily).to.be.a('string').and.not.equal('');
            expect(hardwareAttempt.encoderTitle).to.be.a('string').and.not.equal('');
            expect(hardwareAttempt.width).to.be.greaterThan(0);
            expect(hardwareAttempt.height).to.be.greaterThan(0);
            expect(hardwareAttempt.fpsNum).to.be.greaterThan(0);
            expect(hardwareAttempt.fpsDen).to.be.greaterThan(0);
            expect(response.events.some(event =>
                (event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_x264' ||
                    event.code === 'hardware_validating_target_cadence') &&
                event.width === 1920 && event.height === 1080 && event.fpsNum === 60 && event.fpsDen === 1)).to.equal(true);
            const qualityInput = response.events.find(event => event.code === 'recommendation_selecting_quality');
            if (!qualityInput) throw new Error('Expected a quality-selection input event');
            expect(qualityInput.availableBitrateKbps).to.equal(2500);
            // This estimate-only path may test the higher canvas ceiling, but
            // cannot promote without a successful provider bandwidth probe.
            expect(qualityInput.width).to.equal(1280);
            const qualityResult = response.events.find(event => event.code === 'recommendation_quality_selected');
            if (!qualityResult) throw new Error('Expected a quality-selection result event');
            expect(qualityResult.selectedBitrateKbps).to.equal(2500);
            expect(response.result.legs[0].recommendation.width).to.be.at.most(1280);
            expect(response.result.legs[0].recommendation.height).to.be.at.most(720);
            expect(response.result.legs[0].recommendation.fpsNum).to.equal(30);
            expect(response.result.legs[0].recommendation.fpsDen).to.equal(1);
            expect(response.result.legs[0].recommendation.encoderFamily).to.be.a('string').and.not.equal('');
            expect(response.result.legs[0].recommendation.encoderTitle).to.be.a('string').and.not.equal('');
            const testedPresets: Record<string, string> = {
                obs_nvenc_h264_tex: 'p5',
                obs_qsv11_v2: 'TU4',
                h264_texture_amf: 'quality',
                'com.apple.videotoolbox.videoencoder.h264.gva': 'high',
                'com.apple.videotoolbox.videoencoder.ave.avc': 'high',
                obs_x264: 'veryfast',
            };
            const recommendation = response.result.legs[0].recommendation;
            const locallyExpectedEncoder = process.env.AUTOCONFIG_EXPECT_ENCODER;
            if (locallyExpectedEncoder) expect(recommendation.encoderId).to.equal(locallyExpectedEncoder);
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
                schemaVersion: 1,
                topology: 'direct-single',
                legs: [leg({ destinations: [{ platform: 'youtube' }] })],
                activeProbes: [{
                    probeId: 'youtube-primary',
                    kind: 'youtube-unbound',
                    legId: 'primary',
                    serviceName: 'YouTube - RTMPS',
                    server: `rtmps://127.0.0.1:${mockPort}/live2`,
                    streamKey: secret,
                }],
            });

            expect(mock.getConnections()).to.equal(0);
            expect(mock.getBytes()).to.equal(0);
            expect(response.result.legs[0].measurement.mode).to.equal('estimated');
            expect(JSON.stringify(response.result)).not.to.contain(secret);
            expect(JSON.stringify(response.events)).not.to.contain(secret);
            expect(response.events.some(event => event.code === 'active_probe_not_eligible')).to.equal(true);
        } finally {
            await mock.close();
        }
    });

    it('requires the exact YouTube RTMPS service identity before a probe is eligible', async function() {
        const request = {
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({ destinations: [{ platform: 'youtube' }] })],
            activeProbes: [{
                probeId: 'youtube-missing-service',
                kind: 'youtube-unbound',
                legId: 'primary',
                server: 'rtmps://a.rtmps.youtube.com/live2',
                streamKey: 'not-a-real-key',
            }],
        } as unknown as IAutoConfigRequest;

        const response = await run(request);
        expect(response.result.legs[0].measurement.mode).to.equal('estimated');
        expect(response.events.some(event => event.code === 'active_probe_not_eligible')).to.equal(true);
        expect(response.events.some(event => event.code === 'youtube_probe_started')).to.equal(false);
    });

    it('keeps a shared cloud leg estimate-only when no supplied probe is eligible', async function() {
        const response = await run({
            schemaVersion: 1,
            topology: 'cloud-multistream',
            legs: [leg({
                destinations: [{ platform: 'twitch' }, { platform: 'youtube' }],
                estimateReason: 'cloud_multistream',
            })],
            activeProbes: [{
                probeId: 'cloud-twitch-only',
                kind: 'twitch-standard',
                legId: 'primary',
                serviceName: 'Twitch',
                // Individual endpoint validation must reject this credential
                // without affecting the partial-provider coverage policy.
                server: `rtmp://127.0.0.1:${mockPort}/live`,
                streamKey: 'incomplete-cloud-secret',
            }],
        });
        expect(response.result.legs[0].measurement.mode).to.equal('estimated');
        expect(response.result.legs[0].measurement.reason).to.equal('cloud_multistream');
        expect(response.events.some(event => event.code === 'twitch_probe_started')).to.equal(false);
        expect(JSON.stringify(response.result)).not.to.contain('incomplete-cloud-secret');
        expect(JSON.stringify(response.events)).not.to.contain('incomplete-cloud-secret');
    });

    it('default-denies independent dual-output active probes instead of recommending full uplink per leg', async function() {
        const twitchSecret = 'dual-twitch-secret';
        const youtubeSecret = 'dual-youtube-secret';
        const response = await run({
            schemaVersion: 1,
            topology: 'dual-output',
            legs: [
                leg({
                    legId: 'horizontal',
                    display: 'horizontal',
                    destinations: [{ platform: 'twitch' }],
                    current: { ...leg().current, bitrateKbps: 6000 },
                    estimateReason: 'dual_output',
                }),
                leg({
                    legId: 'vertical',
                    display: 'vertical',
                    destinations: [{ platform: 'youtube' }],
                    current: { ...leg().current, bitrateKbps: 6000 },
                    estimateReason: 'dual_output',
                }),
            ],
            activeProbes: [
                {
                    probeId: 'dual-twitch',
                    kind: 'twitch-standard',
                    legId: 'horizontal',
                    serviceName: 'Twitch',
                    server: 'rtmp://live.twitch.tv/app',
                    streamKey: twitchSecret,
                },
                {
                    probeId: 'dual-youtube',
                    kind: 'youtube-unbound',
                    legId: 'vertical',
                    serviceName: 'YouTube - RTMPS',
                    server: 'rtmps://a.rtmps.youtube.com/live2',
                    streamKey: youtubeSecret,
                },
            ],
        });

        expect(response.result.legs).to.have.length(2);
        expect(response.result.legs.every(resultLeg => resultLeg.measurement.mode === 'estimated')).to.equal(true);
        expect(response.result.legs.every(resultLeg => resultLeg.measurement.reason === 'dual_output')).to.equal(true);
        expect(response.events.filter(event => event.code === 'dual_output_multiple_active_legs')).to.have.length(2);
        expect(response.events.some(event => event.code === 'twitch_probe_started' || event.code === 'youtube_probe_started')).to.equal(false);
        expect(new Set(response.result.legs.map(resultLeg => resultLeg.recommendation.encoderId)).size).to.equal(1);
        expect(JSON.stringify(response.result)).not.to.contain(twitchSecret);
        expect(JSON.stringify(response.result)).not.to.contain(youtubeSecret);
        expect(JSON.stringify(response.events)).not.to.contain(twitchSecret);
        expect(JSON.stringify(response.events)).not.to.contain(youtubeSecret);
    });

    it('clamps estimate-only results to bundled platform caps without raising current bitrate', async function() {
        const twitch = await run({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({
                destinations: [{ platform: 'twitch' }],
                current: { ...leg().current, bitrateKbps: 8000 },
                estimateReason: 'probe_disabled',
            })],
        });
        expect(twitch.result.legs[0].measurement.mode).to.equal('estimated');
        expect(twitch.result.legs[0].recommendation.bitrateKbps).to.equal(6000);
        expect(twitch.result.legs[0].limits.maxBitrateKbps).to.equal(6000);

        const alreadyConservative = await run({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({
                destinations: [{ platform: 'twitch' }],
                current: { ...leg().current, bitrateKbps: 2500 },
                estimateReason: 'probe_disabled',
            })],
        });
        expect(alreadyConservative.result.legs[0].recommendation.bitrateKbps).to.equal(2500);
    });

    it('uses the strictest destination/request cap and replaces an unavailable encoder', async function() {
        const response = await run({
            schemaVersion: 1,
            topology: 'cloud-multistream',
            legs: [leg({
                destinations: [{ platform: 'youtube' }, { platform: 'twitch' }],
                current: {
                    ...leg().current,
                    bitrateKbps: 9000,
                    encoderId: 'definitely-not-a-real-encoder',
                },
                limits: { maxBitrateKbps: 1800 },
                estimateReason: 'cloud_multistream',
            })],
        });

        expect(response.result.legs[0].recommendation.bitrateKbps).to.equal(1800);
        expect(response.result.legs[0].recommendation.encoderId).not.to.equal('definitely-not-a-real-encoder');
        expect(response.result.legs[0].recommendation.codec).to.equal('h264');
        expect(['obs_nvenc_h264_tex', 'qsv', 'amd', 'apple', 'x264']).to.include(response.result.legs[0].recommendation.encoderFamily);
        expect(response.result.legs[0].measurement.confidence).to.equal('medium');
    });

    it('does not test or replace provider-owned Twitch both-display encoding', async function() {
        const response = await run({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({
                display: 'both',
                destinations: [{ platform: 'twitch' }],
                current: {
                    ...leg().current,
                    encoderId: 'obs_nvenc_av1_tex',
                    codec: 'av1',
                },
                estimateReason: 'enhanced_broadcasting',
            })],
        });

        expect(response.events.some(event => event.code === 'hardware_provider_managed')).to.equal(true);
        expect(response.events.some(event =>
            event.code === 'hardware_testing_encoder' || event.code === 'hardware_testing_encoder_surfaces' ||
            event.code === 'hardware_validating_target_cadence' || event.code === 'hardware_testing_x264')).to.equal(false);
        expect(response.events.some(event => event.code === 'recommendation_provider_managed')).to.equal(true);
        expect(response.result.legs[0].recommendation.encoderId).to.equal('obs_nvenc_av1_tex');
        expect(response.result.legs[0].recommendation.codec).to.equal('av1');
    });

    it('preserves a paired vertical recommendation when Enhanced Broadcasting is estimate-only', async function() {
        const response = await run({
            schemaVersion: 1,
            topology: 'enhanced-broadcasting',
            legs: [leg({
                display: 'both',
                destinations: [{ platform: 'twitch' }],
                estimateReason: 'enhanced_broadcasting',
                additionalVideo: {
                    display: 'vertical',
                    current: {
                        ...leg().current,
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

        expect(response.result.legs[0].measurement.mode).to.equal('estimated');
        expect(response.result.legs[0].recommendation.additionalVideo).to.deep.equal({
            display: 'vertical',
            width: 720,
            height: 1280,
            fpsNum: 60,
            fpsDen: 1,
        });
        expect(response.events.some(event => event.code === 'recommendation_provider_managed')).to.equal(true);
    });

    it('cancels a prepared session and makes cleanup observable before returning', async function() {
        const events: IAutoConfigEvent[] = [];
        const sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'custom-rtmp',
            legs: [leg()],
        } as IAutoConfigRequest), (event: IAutoConfigEvent) => events.push(event));

        osn.NodeObs.CancelAutoConfigSession(sessionId);
        const result = JSON.parse(osn.NodeObs.GetAutoConfigResult(sessionId)) as IAutoConfigResult;
        expect(result.status).to.equal('cancelled');
        expect(result.error.code).to.equal('cancelled');
        osn.NodeObs.CloseAutoConfigSession(sessionId);
    });

    it('cancels a running session only after its scratch worker has cleaned up', async function() {
        const events: IAutoConfigEvent[] = [];
        const sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'custom-rtmp',
            legs: [leg()],
        } as IAutoConfigRequest), (event: IAutoConfigEvent) => events.push(event));

        osn.NodeObs.StartAutoConfigSession(sessionId);
        // Let the worker enter the hardware phase so this covers cancellation
        // of an executing scratch workload rather than a prepared session.
        await new Promise(resolve => setTimeout(resolve, 100));

        osn.NodeObs.CancelAutoConfigSession(sessionId);
        const result = JSON.parse(osn.NodeObs.GetAutoConfigResult(sessionId)) as IAutoConfigResult;
        expect(result.status).to.equal('cancelled');
        expect(result.error.code).to.equal('cancelled');
        osn.NodeObs.CloseAutoConfigSession(sessionId);
    });

    it('cancels an active session before the legacy service resets its video context', async function() {
        const sessionId = await startSessionAtHardwareAttempt();

        try {
            expect(() => osn.NodeObs.OBS_service_resetVideoContext()).not.to.throw();

            const result = JSON.parse(osn.NodeObs.GetAutoConfigResult(sessionId)) as IAutoConfigResult;
            expect(result.status).to.equal('cancelled');
            expect(result.error.code).to.equal('cancelled');
        } finally {
            try { osn.NodeObs.CancelAutoConfigSession(sessionId); } catch (_) { /* already cancelled */ }
            try { osn.NodeObs.CloseAutoConfigSession(sessionId); } catch (_) { /* already closed */ }
        }
    });

    it('cancels an active session before Advanced settings reset video', async function() {
        const advancedSettings = obs.getSettingsContainer('Advanced');
        const sessionId = await startSessionAtHardwareAttempt();

        try {
            expect(() => obs.setSettingsContainer('Advanced', advancedSettings)).not.to.throw();

            const result = JSON.parse(osn.NodeObs.GetAutoConfigResult(sessionId)) as IAutoConfigResult;
            expect(result.status).to.equal('cancelled');
            expect(result.error.code).to.equal('cancelled');
        } finally {
            try { osn.NodeObs.CancelAutoConfigSession(sessionId); } catch (_) { /* already cancelled */ }
            try { osn.NodeObs.CloseAutoConfigSession(sessionId); } catch (_) { /* already closed */ }
        }
    });

    it('rejects malformed versioned requests before creating a session', function() {
        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 999,
            topology: 'direct-single',
            legs: [leg()],
        }), () => undefined)).to.throw('unsupported_autoconfig_schema');

        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({ limits: { maxWidth: 1920 } })],
        }), () => undefined)).to.throw('invalid_autoconfig_limits');

        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({ limits: { maxWidth: 4294969216, maxHeight: 4294968376 } })],
        }), () => undefined)).to.throw('invalid_autoconfig_limits');

        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({ limits: { maxFpsNum: 60, maxFpsDen: -1 } })],
        }), () => undefined)).to.throw('invalid_autoconfig_limits');

        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'direct-single',
            legs: [leg({ limits: { maxFpsDen: 1 } })],
        }), () => undefined)).to.throw('invalid_autoconfig_limits');

        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'enhanced-broadcasting',
            legs: [leg({
                display: 'horizontal',
                additionalVideo: {
                    display: 'vertical',
                    current: { ...leg().current, width: 720, height: 1280 },
                },
            })],
        }), () => undefined)).to.throw('invalid_autoconfig_additional_video');

        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'enhanced-broadcasting',
            legs: [leg({
                display: 'both',
                additionalVideo: {
                    display: 'vertical',
                    current: { ...leg().current, width: 720, height: 1280 },
                    limits: { maxWidth: 1080 },
                },
            })],
        }), () => undefined)).to.throw('invalid_autoconfig_additional_video');
    });

    it('rejects more upload legs than Desktop can create', function() {
        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'dual-output',
            legs: [
                leg({ legId: 'horizontal', display: 'horizontal' }),
                leg({ legId: 'vertical', display: 'vertical' }),
                leg({ legId: 'unexpected-third', display: 'horizontal' }),
            ],
        } as IAutoConfigRequest), () => undefined)).to.throw('invalid_autoconfig_legs');
    });

    it('cancels an active session before removing its video context', async function() {
        const sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'custom-rtmp',
            legs: [leg()],
        } as IAutoConfigRequest), () => undefined);

        osn.NodeObs.StartAutoConfigSession(sessionId);
        await new Promise(resolve => setTimeout(resolve, 100));

        const startedAt = Date.now();
        obs.destroyDefaultVideoContext();
        expect(Date.now() - startedAt).to.be.lessThan(15000);

        const result = JSON.parse(osn.NodeObs.GetAutoConfigResult(sessionId)) as IAutoConfigResult;
        expect(result.status).to.equal('cancelled');
        expect(result.error.code).to.equal('cancelled');
        osn.NodeObs.CloseAutoConfigSession(sessionId);
        obs.createDefaultVideoContext();
    });

    it('disconnects cleanly while a session worker is running', async function() {
        const sessionId = osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 1,
            topology: 'custom-rtmp',
            legs: [leg()],
        } as IAutoConfigRequest), () => undefined);

        osn.NodeObs.StartAutoConfigSession(sessionId);
        await new Promise(resolve => setTimeout(resolve, 100));

        const startedAt = Date.now();
        obs.shutdown();
        obs = null;
        expect(Date.now() - startedAt).to.be.lessThan(15000);
    });
});
