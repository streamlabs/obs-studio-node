// Mock RTMP server for autoconfig bandwidth tests, backed by node-media-server.
//
// node-media-server handles the full RTMP protocol (handshake, AMF
// connect/createStream/publish) so libOBS transitions into "publishing" state
// and produces real obs_output_get_total_bytes() values.
//
// Usage:
//     const mock = await startMockRtmp(11935);
//     // ... run autoconfig pointed at rtmp://127.0.0.1:11935/live ...
//     expect(mock.getConnections()).to.be.greaterThan(0);
//     await mock.close();

import * as net from 'net';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const NodeMediaServer = require('node-media-server');

export interface IMockRtmp {
    port: number;
    getBytes: () => number;
    getConnections: () => number;
    close: () => Promise<void>;
}

function waitForPort(port: number, timeoutMs: number = 10000): Promise<void> {
    const start = Date.now();
    return new Promise((resolve, reject) => {
        function tryConnect() {
            if (Date.now() - start > timeoutMs) {
                reject(new Error(`Timed out waiting for port ${port} to open`));
                return;
            }
            const sock = new net.Socket();
            sock.once('connect', () => {
                sock.destroy();
                resolve();
            });
            sock.once('error', () => {
                sock.destroy();
                setTimeout(tryConnect, 50);
            });
            sock.connect(port, '127.0.0.1');
        }
        tryConnect();
    });
}

export async function startMockRtmp(port: number): Promise<IMockRtmp> {
    let connections = 0;
    let totalBytes = 0;

    const nms = new NodeMediaServer({
        logType: 0,
        rtmp: {
            port,
            chunk_size: 60000,
            gop_cache: false,
            ping: 0,
            ping_timeout: 60,
        },
    });

    nms.on('postPublish', () => {
        connections++;
    });

    nms.on('postConnect', (_id: string, args: any) => {
        const session = nms.getSession(_id);
        if (session && session.socket) {
            session.socket.on('data', (chunk: Buffer) => {
                totalBytes += chunk.length;
            });
        }
    });

    nms.run();

    // Wait until the RTMP port is actually accepting connections.
    await waitForPort(port);

    return {
        port,
        getBytes: () => totalBytes,
        getConnections: () => connections,
        close: () =>
            new Promise<void>((resolve) => {
                nms.stop();
                resolve();
            }),
    };
}
