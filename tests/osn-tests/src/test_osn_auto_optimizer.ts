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
    this.timeout(80000);

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
            }, 60000);

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
            bandwidthModes: ['twitch-standard-active', 'youtube-unbound-active', 'estimate'],
        });
    });

    it('never dials an ineligible custom RTMP active-probe request', async function() {
        const mock = await startConnectionSink(mockPort);
        const secret = 'must-not-appear-in-result';
        try {
            const response = await run({
                schemaVersion: 1,
                topology: 'custom-rtmp',
                legs: [leg()],
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

    it('requires a complete Twitch and YouTube probe set for a shared cloud leg', async function() {
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
                // The incomplete set must be rejected before any connection
                // attempt, so an intentionally unofficial endpoint is safe.
                server: `rtmp://127.0.0.1:${mockPort}/live`,
                streamKey: 'incomplete-cloud-secret',
            }],
        });
        expect(response.result.legs[0].measurement.mode).to.equal('estimated');
        expect(response.result.legs[0].measurement.reason).to.equal('cloud_multistream');
        expect(response.events.some(event => event.code === 'active_probe_set_incomplete')).to.equal(true);
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
        expect(response.result.legs[0].measurement.confidence).to.equal('medium');
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

    it('rejects malformed versioned requests before creating a session', function() {
        expect(() => osn.NodeObs.CreateAutoConfigSession(JSON.stringify({
            schemaVersion: 999,
            topology: 'direct-single',
            legs: [leg()],
        }), () => undefined)).to.throw('unsupported_autoconfig_schema');
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
