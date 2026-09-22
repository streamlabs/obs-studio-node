// The RTMP/AMF0 subset used by the local integration-test receiver. Unsupported
// messages fail explicitly so a parser gap cannot look like missing media.
export interface IRtmpMessage {
    type: number;
    streamId: number;
    timestamp: number;
    payload: Buffer;
}

interface IChunkStream extends IRtmpMessage {
    delta: number;
    extended: boolean;
    length: number;
    received: number;
}

const MAX_MESSAGE_SIZE = 8 * 1024 * 1024;
const MAX_CHUNK_STREAMS = 32;

export class RtmpChunkDecoder {
    private buffer = Buffer.alloc(0);
    private streams = new Map<number, IChunkStream>();
    private chunkSize = 128;

    constructor(private onMessage: (message: IRtmpMessage) => void) {}

    write(data: Buffer) {
        this.buffer = Buffer.concat([this.buffer, data]);
        if (this.buffer.length > MAX_MESSAGE_SIZE) throw new Error('RTMP receive buffer exceeded its limit');
        while (this.readChunk()) { /* Consume all complete chunks, retaining partial TCP data. */ }
    }

    private readChunk(): boolean {
        const buffer = this.buffer;
        if (!buffer.length) return false;
        const format = buffer[0] >> 6;
        let csid = buffer[0] & 63;
        const basicLength = csid === 0 ? 2 : csid === 1 ? 3 : 1;
        if (buffer.length < basicLength) return false;
        if (csid === 0) csid = buffer[1] + 64;
        else if (csid === 1) csid = buffer[1] + buffer[2] * 256 + 64;
        const headerLength = basicLength + [11, 7, 3, 0][format];
        if (buffer.length < headerLength) return false;
        const previous = this.streams.get(csid);
        if (format !== 0 && !previous) throw new Error(`Missing RTMP chunk header for stream ${csid}`);
        const continuing = previous && previous.received < previous.length;
        if (continuing && format !== 3) throw new Error('RTMP message interrupted without an abort');
        const timestampField = format === 3 ? 0 : buffer.readUIntBE(basicLength, 3);
        const extended = format === 3 ? previous.extended : timestampField === 0xffffff;
        const payloadOffset = headerLength + (extended ? 4 : 0);
        if (buffer.length < payloadOffset) return false;
        const time = extended ? buffer.readUInt32BE(headerLength) : timestampField;
        const length = format < 2 ? buffer.readUIntBE(basicLength + 3, 3) : previous.length;
        if (length > MAX_MESSAGE_SIZE) throw new Error(`RTMP message is too large: ${length}`);
        const received = continuing ? previous.received : 0;
        const count = Math.min(this.chunkSize, length - received);
        if (buffer.length < payloadOffset + count) return false;

        let state = previous;
        if (!continuing) {
            if (!previous && this.streams.size >= MAX_CHUNK_STREAMS) throw new Error('Too many RTMP chunk streams');
            state = {
                type: format < 2 ? buffer[basicLength + 6] : previous.type,
                streamId: format === 0 ? buffer.readUInt32LE(basicLength + 7) : previous.streamId,
                timestamp: format === 0 ? time : (previous.timestamp + (format === 3 ? previous.delta : time)) >>> 0,
                delta: format === 3 ? previous.delta : time,
                extended, length, received: 0, payload: Buffer.alloc(length),
            };
            this.streams.set(csid, state);
        }
        buffer.copy(state.payload, state.received, payloadOffset, payloadOffset + count);
        state.received += count;
        this.buffer = buffer.subarray(payloadOffset + count);
        if (state.received === state.length) {
            if (state.type === 1) {
                if (state.length !== 4) throw new Error('Invalid RTMP chunk-size message');
                const size = state.payload.readUInt32BE(0);
                if (!size || size > 65536) throw new Error(`Unsupported RTMP chunk size: ${size}`);
                this.chunkSize = size;
            } else if (state.type === 2) {
                if (state.length !== 4) throw new Error('Invalid RTMP abort message');
                const aborted = this.streams.get(state.payload.readUInt32BE(0));
                if (aborted) aborted.received = aborted.length;
            }
            this.onMessage({ type: state.type, streamId: state.streamId, timestamp: state.timestamp, payload: state.payload });
        }
        return true;
    }
}

export function encodeRtmpMessage(type: number, payload: Buffer, streamId = 0): Buffer {
    const csid = type === 20 ? 3 : 2;
    const header = Buffer.alloc(12);
    header[0] = csid;
    header.writeUIntBE(payload.length, 4, 3);
    header[7] = type;
    header.writeUInt32LE(streamId, 8);
    const parts: Buffer[] = [header];
    for (let i = 0; i < payload.length; i += 128) {
        if (i) parts.push(Buffer.from([0xc0 | csid]));
        parts.push(payload.subarray(i, i + 128));
    }
    return Buffer.concat(parts);
}

export function encodeAmf(value: any): Buffer {
    if (typeof value === 'string') {
        const text = Buffer.from(value);
        const header = Buffer.alloc(3);
        header[0] = 2;
        header.writeUInt16BE(text.length, 1);
        return Buffer.concat([header, text]);
    }
    if (typeof value === 'number') {
        const result = Buffer.alloc(9);
        result.writeDoubleBE(value, 1);
        return result;
    }
    if (typeof value === 'boolean') return Buffer.from([1, Number(value)]);
    if (value == null) return Buffer.from([5]);
    const parts: Buffer[] = [Buffer.from([3])];
    for (const key of Object.keys(value)) {
        const text = Buffer.from(key);
        const length = Buffer.alloc(2);
        length.writeUInt16BE(text.length, 0);
        parts.push(length, text, encodeAmf(value[key]));
    }
    return Buffer.concat([...parts, Buffer.from([0, 0, 9])]);
}

export function decodeAmf(payload: Buffer): any[] {
    let offset = 0;
    const take = (size: number) => {
        if (offset + size > payload.length) throw new Error('Truncated AMF message');
        const result = payload.subarray(offset, offset + size);
        offset += size;
        return result;
    };
    const string = (long = false) => take(long ? take(4).readUInt32BE(0) : take(2).readUInt16BE(0)).toString('utf8');
    const read = (depth: number): any => {
        if (depth > 16) throw new Error('AMF nesting exceeded its limit');
        const type = take(1)[0];
        switch (type) {
            case 0: return take(8).readDoubleBE(0);
            case 1: return take(1)[0] !== 0;
            case 2: return string();
            case 5: case 6: return null;
            case 12: return string(true);
            case 3: case 8: {
                if (type === 8) take(4);
                const result = Object.create(null);
                while (true) {
                    const key = string();
                    if (!key && payload[offset] === 9) { take(1); return result; }
                    result[key] = read(depth + 1);
                }
            }
            case 10: {
                const length = take(4).readUInt32BE(0);
                if (length > payload.length - offset) throw new Error('Invalid AMF array length');
                const result = [];
                for (let i = 0; i < length; i++) result.push(read(depth + 1));
                return result;
            }
            default: throw new Error(`Unsupported AMF type: ${type}`);
        }
    };
    const values = [];
    while (offset < payload.length) values.push(read(0));
    return values;
}

export interface IRtmpMediaPacket {
    kind: 'audio' | 'video';
    codec: string;
    trackId: number;
    packetType: number;
    sequenceHeader: boolean;
    timestamp: number;
    size: number;
}

export function decodeMediaPacket(message: IRtmpMessage): IRtmpMediaPacket {
    const { payload, timestamp } = message;
    const audio = message.type === 8;
    if (!audio && message.type !== 9) throw new Error('Expected RTMP audio or video');
    if (payload.length < 2) throw new Error('Truncated RTMP media header');
    const extended = audio ? (payload[0] >> 4) === 9 : !!(payload[0] & 0x80);
    let codec: string;
    let trackId = 0;
    let packetType = payload[1];
    if (extended) {
        packetType = payload[0] & 15;
        const multitrack = packetType === (audio ? 5 : 6);
        if (payload.length < (multitrack ? 7 : 5)) throw new Error('Truncated enhanced RTMP media header');
        if (multitrack) {
            if ((payload[1] >> 4) !== 0) throw new Error('Only RTMP one-track-per-message encoding is supported');
            packetType = payload[1] & 15;
            trackId = payload[6];
        }
        codec = payload.toString('ascii', multitrack ? 2 : 1, multitrack ? 6 : 5);
    } else {
        const codecId = audio ? payload[0] >> 4 : payload[0] & 15;
        if (codecId !== (audio ? 10 : 7)) throw new Error(`Unsupported legacy RTMP codec: ${codecId}`);
        codec = audio ? 'mp4a' : 'avc1';
    }
    return { kind: audio ? 'audio' : 'video', codec, trackId, packetType, sequenceHeader: packetType === 0, timestamp, size: payload.length };
}
