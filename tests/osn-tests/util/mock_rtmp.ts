// Tiny mock RTMP listener for the autoconfig bandwidth test.
//
// The bandwidth test only needs the connection to stay open and to actually receive
// data — nothing downstream consumes the stream. So we accept the connection, echo
// back the RTMP handshake (the only fixed-size handshake exchange the libobs RTMP
// output requires before it starts pushing payload), and drain everything else into
// a counter.
//
// Handshake reference: RTMP 1.0 spec §5.2. C0 (1 byte version) + C1 (1536 bytes
// time + zero + random). Server replies S0+S1 (mirror) + S2 (echo C1). Client then
// sends C2 (echo S1). After that, AMF chunks start. We don't validate AMF — we just
// drain.
//
// Usage:
//     const mock = await startMockRtmp(1935);
//     // ... run autoconfig pointed at rtmp://127.0.0.1:1935/live ...
//     expect(mock.getBytes()).to.be.greaterThan(0);
//     await mock.close();

import * as net from 'net';

export interface IMockRtmp {
    port: number;
    getBytes: () => number;
    getConnections: () => number;
    close: () => Promise<void>;
}

const HANDSHAKE_PAYLOAD_SIZE = 1536;
const HANDSHAKE_TOTAL_SIZE = 1 + HANDSHAKE_PAYLOAD_SIZE; // C0 + C1 / S0 + S1

export async function startMockRtmp(port: number): Promise<IMockRtmp> {
    let totalBytes = 0;
    let connectionCount = 0;
    const liveSockets = new Set<net.Socket>();

    const server = net.createServer((socket) => {
        connectionCount++;
        liveSockets.add(socket);

        let handshakeBuf = Buffer.alloc(0);
        let handshakeDone = false;

        socket.on('data', (chunk) => {
            totalBytes += chunk.length;

            if (!handshakeDone) {
                handshakeBuf = Buffer.concat([handshakeBuf, chunk]);
                if (handshakeBuf.length >= HANDSHAKE_TOTAL_SIZE) {
                    // Reply with S0 (= C0 byte) + S1 (1536 random bytes) + S2 (echo of C1).
                    const s0 = handshakeBuf.subarray(0, 1);
                    const c1 = handshakeBuf.subarray(1, HANDSHAKE_TOTAL_SIZE);
                    const s1 = Buffer.alloc(HANDSHAKE_PAYLOAD_SIZE);
                    socket.write(Buffer.concat([s0, s1, c1]));
                    handshakeDone = true;
                    handshakeBuf = Buffer.alloc(0);
                }
            }
            // After handshake, drop the bytes — the bandwidth test only cares that
            // the connection accepts data.
        });

        const drop = () => liveSockets.delete(socket);
        socket.on('close', drop);
        socket.on('error', drop);
    });

    await new Promise<void>((resolve, reject) => {
        server.once('error', reject);
        server.listen(port, '127.0.0.1', () => {
            server.removeListener('error', reject);
            resolve();
        });
    });

    return {
        port,
        getBytes: () => totalBytes,
        getConnections: () => connectionCount,
        close: () =>
            new Promise<void>((resolve) => {
                // server.close() refuses to fire its callback while any client socket
                // is still open. The bandwidth-test cancel path leaves the RTMP socket
                // half-closed on the libobs side, so destroy them here before waiting.
                liveSockets.forEach((s) => s.destroy());
                liveSockets.clear();
                server.close(() => resolve());
            }),
    };
}
