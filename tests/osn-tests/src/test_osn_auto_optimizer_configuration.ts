import 'mocha';
import { expect } from 'chai';
import { createServer, Server } from 'http';
import { AddressInfo, Socket } from 'net';
import * as osn from '../osn';
import type { IAutoOptimizerEvent, IAutoOptimizerRequest } from '../../../js/module';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';

describe('osn-auto-optimizer-configuration', function() {
    this.timeout(30000);

    let obs: OBSHandler;
    let proxy: Server;
    const sockets = new Set<Socket>();
    let requests = 0;
    let stall = true;
    let configurationAuthority = '';
    let onRequest: (() => void) | undefined;

    before(async function() {
        // Intercept HTTPS CONNECT locally: never forward a request or credentials
        // to Twitch. A stalled tunnel exercises the real configuration timeout.
        proxy = createServer((_, response) => response.writeHead(502).end());
        proxy.on('connection', socket => {
            sockets.add(socket);
            socket.on('error', () => undefined); // libcurl may abort a timed-out connection.
            socket.on('close', () => sockets.delete(socket));
        });
        proxy.on('connect', (incoming, socket) => {
            // Background service updates must not count as configuration attempts.
            if (incoming.url !== configurationAuthority) {
                socket.end('HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n');
                return;
            }
            requests++;
            if (onRequest) onRequest();
            if (!stall) socket.end('HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n');
        });
        await new Promise<void>((resolve, reject) => {
            proxy.once('error', reject);
            proxy.listen(0, '127.0.0.1', resolve);
        });
        const proxyUrl = `http://127.0.0.1:${(proxy.address() as AddressInfo).port}`;
        const proxyVariables = ['HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'NO_PROXY',
            'http_proxy', 'https_proxy', 'all_proxy', 'no_proxy'];
        const savedEnvironment = proxyVariables.map(name => [name, process.env[name]] as const);
        try {
            for (const name of proxyVariables)
                process.env[name] = name.toLowerCase() === 'no_proxy' ? '' : proxyUrl;
            deleteConfigFiles();
            // Only the child OSN process keeps this environment. Restore the test
            // runner's proxy settings immediately after starting that process.
            obs = new OBSHandler('osn-auto-optimizer-configuration');
        } finally {
            for (const [name, value] of savedEnvironment) {
                if (value === undefined) delete process.env[name];
                else process.env[name] = value;
            }
        }
        const service = osn.ServiceFactory.create('rtmp_common', 'configuration-test', { service: 'Twitch', server: 'auto' });
        try {
            const url = new URL(service.settings.multitrack_video_configuration_url as string);
            configurationAuthority = `${url.hostname}:${url.port || '443'}`;
        } finally {
            osn.ServiceFactory.destroy(service);
        }
    });

    after(async function() {
        try {
            if (obs) obs.shutdown();
        } finally {
            sockets.forEach(socket => socket.destroy());
            if (proxy?.listening) await new Promise<void>(resolve => proxy.close(() => resolve()));
            deleteConfigFiles();
        }
    });

    beforeEach(function() {
        requests = 0;
        stall = true;
        onRequest = undefined;
    });

    function request(width: number, height: number): IAutoOptimizerRequest {
        return {
            streamSetup: 'enhanced-broadcasting',
            outputs: [{
                outputId: 'primary', display: 'horizontal',
                outputKind: 'twitch-enhanced-broadcasting', destinations: ['twitch'],
                current: {
                    canvasId: obs.defaultVideoContext.canvasId,
                    width: 1920, height: 1080, fpsNum: 30, fpsDen: 1,
                    bitrateKbps: 6000, encoderId: 'obs_x264', preset: 'veryfast',
                },
                limits: { maxWidth: width, maxHeight: height },
                probes: [{ id: 'twitch', kind: 'twitch-enhanced-broadcasting', streamKey: 'local-test-key' }],
            }],
        };
    }

    for (const [width, height] of [[2560, 1440], [1920, 1080]]) {
        it(`cancels a stalled ${width}x${height} configuration request without retrying`, async function() {
            let timer: ReturnType<typeof setTimeout>;
            const run = osn.NodeObs.AutoOptimizer.run(request(width, height), () => undefined);
            // Attach immediately so the pre-fix cleanup timeout is reported as a
            // test assertion failure, not an unhandled rejection while cancelling.
            run.result.catch(() => undefined);
            try {
                await new Promise<void>((resolve, reject) => {
                    timer = setTimeout(() => reject(new Error('No configuration request reached the local proxy')), 15000);
                    onRequest = () => { clearTimeout(timer); resolve(); };
                });
                await run.cancel();
                // A cleanup taking over the native eight-second deadline rejects
                // this promise instead of returning a cancelled result.
                const result = await run.result;
                expect(result.status).to.equal('cancelled');
                expect(result.error.code).to.equal('cancelled');
                expect(requests).to.equal(1);

                const nextRequest = request(1920, 1080);
                nextRequest.outputs[0].probes = [];
                const nextRun = osn.NodeObs.AutoOptimizer.run(nextRequest, () => undefined);
                try {
                    expect((await nextRun.result).status).to.equal('complete');
                } finally {
                    await nextRun.cancel();
                }
            } finally {
                clearTimeout(timer!);
                onRequest = undefined;
                await run.cancel();
            }
        });
    }

    it('still retries a failed 1440p configuration at 1080p when not cancelled', async function() {
        stall = false;
        const events: IAutoOptimizerEvent[] = [];
        const run = osn.NodeObs.AutoOptimizer.run(request(2560, 1440), event => events.push(event));
        try {
            const result = await run.result;
            expect(result.status).to.equal('complete');
            expect(result.outputs[0].measurement.mode).to.equal('estimated');
            expect(requests).to.equal(2);
            expect(events.filter(event => event.code === 'enhanced_broadcasting_requesting_ladder' && event.width)
                .map(event => [event.width, event.height])).to.deep.equal([[2560, 1440], [1920, 1080]]);
        } finally {
            await run.cancel();
        }
    });
});
