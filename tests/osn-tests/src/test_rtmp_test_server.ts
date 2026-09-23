import 'mocha';
import { expect } from 'chai';
import { connect, Socket } from 'net';
import { decodeAmf, decodeMediaPacket, encodeAmf, encodeRtmpMessage, IRtmpMessage, RtmpChunkDecoder } from '../util/rtmp-protocol';
import { RtmpTestServer } from '../util/rtmp-test-server';
import { expectAudioTracks, expectVideoFrames } from '../util/rtmp-assertions';

describe('RTMP test receiver', () => {
    it('decodes compressed headers across arbitrary TCP boundaries', () => {
        // Four captured-style AAC messages: full header, same stream, delta only, inherited delta.
        const bytes = Buffer.from(
            '040000640000030801000000af0101' +
            '4400001400000308af0102' +
            '84000005af0103' + 'c4af0104', 'hex');
        for (const fragmentSize of [1, 2, 7, bytes.length]) {
            const messages: IRtmpMessage[] = [];
            const parser = new RtmpChunkDecoder(message => messages.push(message));
            for (let offset = 0; offset < bytes.length; offset += fragmentSize) parser.write(bytes.subarray(offset, offset + fragmentSize));
            expect(messages.map(message => message.timestamp)).to.deep.equal([100, 120, 125, 130]);
            expect(messages.map(message => message.payload.toString('hex'))).to.deep.equal(['af0101', 'af0102', 'af0103', 'af0104']);
        }
    });

    it('handles chunk-size changes, interleaved streams, and extended chunk stream IDs', () => {
        const messages: IRtmpMessage[] = [];
        const parser = new RtmpChunkDecoder(message => messages.push(message));
        parser.write(Buffer.from(
            '02000000000004010000000000000004' + // chunk size = 4
            '000600000000000a0801000000af010203' + // stream 70, first chunk
            '050000000000030802000000af01ff' + // another complete message
            'c00604050607' + 'c0060809', 'hex'));
        expect(messages.map(message => message.payload.toString('hex'))).to.deep.equal(['00000004', 'af01ff', 'af010203040506070809']);
        expect(messages.map(message => message.streamId)).to.deep.equal([0, 2, 1]);
    });

    it('preserves extended absolute timestamps and extended deltas', () => {
        const messages: IRtmpMessage[] = [];
        const parser = new RtmpChunkDecoder(message => messages.push(message));
        parser.write(Buffer.from('04ffffff000003080100000001000000af0101' +
            '84ffffff01000001af0102' + 'c401000001af0103', 'hex'));
        expect(messages.map(message => message.timestamp)).to.deep.equal([0x1000000, 0x2000001, 0x3000002]);
    });

    it('decodes AMF commands and nested connect properties', () => {
        expect(decodeAmf(Buffer.from('0200077075626c69736800000000000000000005020008766572746963616c', 'hex')))
            .to.deep.equal(['publish', 0, null, 'vertical']);
        const properties = { app: 'live', objectEncoding: 0, audio: { enabled: true }, optional: null };
        expect(decodeAmf(encodeAmf(properties))).to.deep.equal([properties]);
        expect(() => decodeAmf(Buffer.from('02001061', 'hex'))).to.throw('Truncated AMF');
        expect(() => decodeAmf(Buffer.from([17]))).to.throw('Unsupported AMF type');
    });

    it('distinguishes primary AAC from enhanced secondary audio headers and frames', () => {
        const decode = (hex: string, type = 8) => decodeMediaPacket({ type, timestamp: 42, streamId: 1, payload: Buffer.from(hex, 'hex') });
        expect(decode('af001210')).to.include({ kind: 'audio', codec: 'mp4a', trackId: 0, sequenceHeader: true });
        expect(decode('af010102')).to.include({ trackId: 0, packetType: 1, sequenceHeader: false });
        expect(decode('95006d703461011210')).to.include({ codec: 'mp4a', trackId: 1, sequenceHeader: true });
        expect(decode('95016d703461010102')).to.include({ trackId: 1, packetType: 1, timestamp: 42, sequenceHeader: false });
        expect(decode('170000000001', 9)).to.include({ kind: 'video', codec: 'avc1', trackId: 0, sequenceHeader: true });
        expect(() => decode('95006d')).to.throw('Truncated enhanced');
        expect(() => decode('95216d70346101')).to.throw('one-track-per-message');
    });

    it('fails explicitly on invalid chunk headers and sizes', () => {
        expect(() => new RtmpChunkDecoder(() => {}).write(Buffer.from('c4', 'hex'))).to.throw('Missing RTMP chunk header');
        expect(() => new RtmpChunkDecoder(() => {}).write(Buffer.from('04000000ffffff0801000000', 'hex'))).to.throw('too large');
        expect(() => new RtmpChunkDecoder(() => {}).write(encodeRtmpMessage(1, Buffer.alloc(4)))).to.throw('chunk size');
    });

    async function publish(server: RtmpTestServer, key: string): Promise<Socket> {
        const address = new URL(server.url);
        const socket = connect(Number(address.port), address.hostname);
        await new Promise<void>((resolve, reject) => { socket.once('connect', resolve); socket.once('error', reject); });
        socket.on('error', () => {}); // Deliberate server disconnects are exercised below.
        socket.resume();
        const command = (values: any[], streamId = 0) => encodeRtmpMessage(20, Buffer.concat(values.map(encodeAmf)), streamId);
        socket.write(Buffer.concat([
            Buffer.from([3]), Buffer.alloc(1536), Buffer.alloc(1536),
            command(['connect', 1, { app: 'live' }]), command(['createStream', 2, null]),
            command(['publish', 0, null, key, 'live'], 1),
        ]));
        return socket;
    }

    it('isolates simultaneous publishers and later attempts, with bounded media waits', async () => {
        const server = await RtmpTestServer.start();
        try {
            const horizontal = await publish(server, 'horizontal');
            const vertical = await publish(server, 'vertical');
            const first = await server.waitForPublish('vertical');
            horizontal.write(encodeRtmpMessage(8, Buffer.from('95016d70346101ff', 'hex'), 1));
            vertical.write(Buffer.concat([
                encodeRtmpMessage(8, Buffer.from('af01ff', 'hex'), 1),
                encodeRtmpMessage(9, Buffer.from('2701000000ff', 'hex'), 1),
            ]));
            await first.waitForMedia({ audioPackets: 1, videoPackets: 1 });
            expect(first.audioTrackIds()).to.deep.equal([0]);
            const other = await server.waitForPublish('horizontal');
            await other.waitForMedia({ audioPackets: 1 });
            expect(other.audioTrackIds()).to.deep.equal([1]);
            first.disconnect();
            await publish(server, 'vertical');
            const next = await server.waitForPublish('vertical', { afterAttempt: first.attempt });
            expect(next.attempt).to.be.greaterThan(first.attempt);
            expect(next.snapshot()).to.deep.equal([]);
            const failure = await next.waitForMedia({ videoPackets: 1 }, 20).then(() => '', error => error.message);
            expect(failure).to.contain('Timed out waiting for RTMP media');
            server.assertHealthy();
        } finally { await server.close(); }
    });

    it('reports parser failures and settles pending waits on shutdown', async () => {
        const server = await RtmpTestServer.start();
        try {
            const client = await publish(server, 'invalid');
            const session = await server.waitForPublish('invalid');
            client.write(encodeRtmpMessage(8, Buffer.from('9500', 'hex'), 1));
            const failure = await session.waitForMedia({ audioPackets: 1 }).then(() => '', error => error.message);
            expect(failure).to.contain('Truncated enhanced');
            expect(() => server.assertHealthy()).to.throw('RTMP attempt');
        } finally { await server.close(); }

        const closing = await RtmpTestServer.start();
        const pending = closing.waitForPublish('missing').then(() => '', error => error.message);
        await closing.close();
        expect(await pending).to.contain('closed while waiting');
        await closing.close();
    });

    it('media assertions require complete audio tracks and encoded video frames', async () => {
        const server = await RtmpTestServer.start();
        try {
            const client = await publish(server, 'assertions');
            const session = await server.waitForPublish('assertions');
            client.write(Buffer.concat([
                encodeRtmpMessage(8, Buffer.from('af01ff', 'hex'), 1),
                encodeRtmpMessage(9, Buffer.from('170000000001', 'hex'), 1),
                encodeRtmpMessage(9, Buffer.from('1702000000', 'hex'), 1),
            ]));
            await session.waitForMedia({ audioPackets: 1 });
            expect(() => expectAudioTracks(session, [0])).to.throw('audio track 0 header');
            expect(() => expectVideoFrames(session)).to.throw('video frames');

            client.write(Buffer.concat([
                encodeRtmpMessage(8, Buffer.from('af001210', 'hex'), 1),
                encodeRtmpMessage(8, Buffer.from('95006d703461011210', 'hex'), 1),
                encodeRtmpMessage(8, Buffer.from('af01ff', 'hex'), 1),
            ]));
            await session.waitForMedia({ audioPackets: 2 });
            expect(() => expectAudioTracks(session, [0, 1])).to.throw('audio track 1 frames');
            expect(() => expectVideoFrames(session)).to.throw('video frames');

            client.write(Buffer.concat([
                encodeRtmpMessage(8, Buffer.from('95016d70346101ff', 'hex'), 1),
                encodeRtmpMessage(9, Buffer.from('2701000000ff', 'hex'), 1),
            ]));
            await session.waitForMedia({ audioPackets: 3, videoPackets: 1 });
            expectAudioTracks(session, [1, 0]);
            expectVideoFrames(session);
            expect(() => expectAudioTracks(session, [0])).to.throw('audio tracks');
            server.assertHealthy();
        } finally { await server.close(); }
    });
});
