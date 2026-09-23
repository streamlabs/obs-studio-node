import { EventEmitter } from 'events';
import { AddressInfo, createServer, Server, Socket } from 'net';
import {
    decodeAmf, decodeMediaPacket, encodeAmf, encodeRtmpMessage,
    IRtmpMediaPacket, IRtmpMessage, RtmpChunkDecoder,
} from './rtmp-protocol';

function waitFor<T>(events: EventEmitter, read: () => T | undefined, error: () => Error | undefined,
                    description: string, timeoutMs: number): Promise<T> {
    return new Promise((resolve, reject) => {
        const finish = (failure?: Error, value?: T) => {
            clearTimeout(timer);
            events.removeListener('change', check);
            if (failure) reject(failure);
            else resolve(value);
        };
        const check = () => {
            try {
                const failure = error();
                if (failure) return finish(failure);
                const value = read();
                if (value !== undefined) finish(undefined, value);
            } catch (failure) { finish(failure); }
        };
        const timer = setTimeout(() => finish(new Error(`Timed out waiting for ${description}`)), timeoutMs);
        events.on('change', check);
        check();
    });
}

/** One TCP connection/publication attempt. Retained starts and reconnects get new captures. */
export class RtmpTestSession {
    private events = new EventEmitter();
    private media: IRtmpMediaPacket[] = [];
    private failure?: Error;
    private ended = false;
    private handshake = Buffer.alloc(0);
    private handshakeStage = 0;
    private decoder: RtmpChunkDecoder;
    private bytesReceived = 0;
    private acknowledged = 0;
    private acknowledgementWindow = 5000000;
    private capturedBytes = 0;
    private app = '';
    private key?: string;
    readonly commands: string[] = [];

    constructor(readonly attempt: number, private socket: Socket, private changed: () => void) {
        this.decoder = new RtmpChunkDecoder(message => this.onMessage(message));
        socket.on('data', data => {
            try {
                this.bytesReceived += data.length;
                this.receive(data);
                if (this.bytesReceived - this.acknowledged >= this.acknowledgementWindow) {
                    this.send(3, this.uint32(this.bytesReceived >>> 0));
                    this.acknowledged = this.bytesReceived;
                }
            } catch (error) { this.fail(error); }
        });
        socket.on('error', error => this.fail(error));
        socket.on('close', () => {
            this.ended = true;
            this.notify();
        });
    }

    get streamKey(): string | undefined { return this.key; }
    get application(): string { return this.app; }
    get closed(): boolean { return this.ended; }
    get error(): Error | undefined { return this.failure; }

    snapshot(): IRtmpMediaPacket[] { return this.media.slice(); }

    audioTrackIds(): number[] {
        return Array.from(new Set(this.media.filter(packet => packet.kind === 'audio').map(packet => packet.trackId))).sort();
    }

    /** Wait for encoded frames (sequence headers alone do not satisfy the condition). */
    waitForMedia(counts: { audioPackets?: number, videoPackets?: number }, timeoutMs = 10000): Promise<IRtmpMediaPacket[]> {
        return waitFor(this.events, () => {
            const audio = this.media.filter(packet => packet.kind === 'audio' && packet.packetType === 1).length;
            const video = this.media.filter(packet => packet.kind === 'video' && (packet.packetType === 1 || packet.packetType === 3)).length;
            if (audio >= (counts.audioPackets || 0) && video >= (counts.videoPackets || 0)) return this.snapshot();
            if (this.ended) throw new Error(`RTMP attempt ${this.attempt} closed before receiving media (${audio} audio, ${video} video)`);
        }, () => this.failure, `RTMP media for ${this.streamKey}, attempt ${this.attempt}`, timeoutMs);
    }

    /** Deliberately drop this publisher, for reconnect tests or teardown. */
    disconnect() { this.socket.destroy(); }

    private notify() {
        this.events.emit('change');
        this.changed();
    }

    private fail(error: Error) {
        if (!this.failure) this.failure = new Error(`RTMP attempt ${this.attempt}: ${error.message}`);
        this.socket.destroy();
        this.notify();
    }

    private uint32(value: number): Buffer {
        const buffer = Buffer.alloc(4);
        buffer.writeUInt32BE(value, 0);
        return buffer;
    }

    private send(type: number, payload: Buffer, streamId = 0) {
        this.socket.write(encodeRtmpMessage(type, payload, streamId));
    }

    private command(values: any[], streamId = 0) {
        this.send(20, Buffer.concat(values.map(encodeAmf)), streamId);
    }

    private receive(data: Buffer) {
        if (this.handshakeStage === 2) { this.decoder.write(data); return; }
        this.handshake = Buffer.concat([this.handshake, data]);
        if (this.handshakeStage === 0) {
            if (this.handshake.length < 1537) return;
            if (this.handshake[0] !== 3) throw new Error('Unsupported RTMP handshake version');
            this.socket.write(Buffer.concat([Buffer.from([3]), Buffer.alloc(1536), this.handshake.subarray(1, 1537)]));
            this.handshake = this.handshake.subarray(1537);
            this.handshakeStage = 1;
        }
        if (this.handshake.length < 1536) return;
        this.handshakeStage = 2;
        const pending = this.handshake.subarray(1536);
        this.handshake = Buffer.alloc(0);
        this.decoder.write(pending);
    }

    private onMessage(message: IRtmpMessage) {
        const { type, payload, streamId } = message;
        switch (type) {
            case 1: case 2: case 3: break; // Chunk size/abort are handled by the decoder; acknowledgements need no reply.
            case 4: {
                if (payload.length < 2) throw new Error('Truncated RTMP user-control message');
                if (payload.readUInt16BE(0) === 6) {
                    if (payload.length !== 6) throw new Error('Invalid RTMP ping');
                    this.send(4, Buffer.concat([Buffer.from([0, 7]), payload.subarray(2)]));
                }
                break;
            }
            case 5:
                if (payload.length !== 4 || !payload.readUInt32BE(0)) throw new Error('Invalid RTMP acknowledgement window');
                this.acknowledgementWindow = payload.readUInt32BE(0);
                break;
            case 6:
                this.send(5, this.uint32(this.acknowledgementWindow));
                break;
            case 8: case 9:
                this.capturedBytes += payload.length;
                if (this.media.length >= 20000 || this.capturedBytes > 32 * 1024 * 1024) throw new Error('RTMP test capture exceeded its limit');
                this.media.push(decodeMediaPacket(message));
                break;
            case 18:
                decodeAmf(payload); // Validate metadata without retaining potentially large data.
                break;
            case 20: {
                const [name, transaction, properties, argument] = decodeAmf(payload);
                if (typeof name !== 'string') throw new Error('Missing RTMP command name');
                if (this.commands.length >= 256) throw new Error('Too many RTMP commands');
                this.commands.push(name);
                switch (name) {
                    case 'connect':
                        this.app = properties?.app || '';
                        this.send(5, this.uint32(this.acknowledgementWindow));
                        this.send(6, Buffer.concat([this.uint32(this.acknowledgementWindow), Buffer.from([2])]));
                        this.command(['_result', transaction, { fmsVer: 'FMS/3,5,7,7009', capabilities: 31 },
                            { level: 'status', code: 'NetConnection.Connect.Success', description: 'Local test receiver', objectEncoding: 0 }]);
                        break;
                    case 'releaseStream': case 'FCPublish':
                        this.command(['_result', transaction, null, null]);
                        break;
                    case 'createStream':
                        this.command(['_result', transaction, null, 1]);
                        break;
                    case 'publish':
                        if (this.key !== undefined || typeof argument !== 'string') throw new Error('Invalid or repeated RTMP publication');
                        this.key = argument;
                        this.command(['onStatus', 0, null, { level: 'status', code: 'NetStream.Publish.Start', description: 'Local test publication' }], streamId);
                        break;
                    case 'FCUnpublish': case 'deleteStream': case 'closeStream': break;
                    default: throw new Error(`Unsupported RTMP command: ${name}`);
                }
                break;
            }
            default: throw new Error(`Unsupported RTMP message type: ${type}`);
        }
        this.notify();
    }
}

/** Credential-free loopback receiver shared by streaming integration tests. */
export class RtmpTestServer {
    private events = new EventEmitter();
    private connections: RtmpTestSession[] = [];
    private server: Server;
    private failure?: Error;
    private closing = false;
    private closePromise?: Promise<void>;

    private constructor() {
        this.server = createServer(socket => {
            if (this.connections.length >= 64) {
                socket.destroy();
                this.failure = new Error('RTMP test server exceeded its connection limit');
            } else {
                this.connections.push(new RtmpTestSession(this.connections.length + 1, socket, () => this.events.emit('change')));
            }
            this.events.emit('change');
        });
        this.server.on('error', error => { this.failure = error; this.events.emit('change'); });
    }

    static async start(): Promise<RtmpTestServer> {
        const receiver = new RtmpTestServer();
        await new Promise<void>((resolve, reject) => {
            receiver.server.once('error', reject);
            receiver.server.listen(0, '127.0.0.1', () => {
                receiver.server.removeListener('error', reject);
                resolve();
            });
        });
        return receiver;
    }

    get url(): string { return `rtmp://127.0.0.1:${(this.server.address() as AddressInfo).port}/live`; }

    sessions(): RtmpTestSession[] { return this.connections.slice(); }

    /** afterAttempt selects a later restart/reconnect instead of returning an earlier publication. */
    waitForPublish(streamKey: string, options: { afterAttempt?: number, timeoutMs?: number } = {}): Promise<RtmpTestSession> {
        return waitFor(this.events,
            () => this.connections.find(session => session.streamKey === streamKey && session.attempt > (options.afterAttempt || 0)),
            () => this.failure || this.connections.find(session => session.error)?.error ||
                (this.closing ? new Error('RTMP test server closed while waiting for a publication') : undefined),
            `RTMP publication ${streamKey} after attempt ${options.afterAttempt || 0}`, options.timeoutMs ?? 10000);
    }

    assertHealthy() {
        const error = this.failure || this.connections.find(session => session.error)?.error;
        if (error) throw error;
    }

    close(): Promise<void> {
        if (this.closePromise) return this.closePromise;
        this.closing = true;
        this.events.emit('change');
        this.closePromise = new Promise<void>((resolve, reject) => {
            this.connections.forEach(session => session.disconnect());
            this.server.close(error => error ? reject(error) : resolve());
        });
        return this.closePromise;
    }
}
