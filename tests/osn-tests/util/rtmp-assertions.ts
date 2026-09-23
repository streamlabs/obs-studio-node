import { expect } from 'chai';
import { RtmpTestSession } from './rtmp-test-server';

/** Asserts the exact audio track set, including a sequence header and encoded frames for each track. */
export function expectAudioTracks(session: RtmpTestSession, ids: number[]): void {
    const packets = session.snapshot();
    const label = `RTMP ${session.streamKey}, attempt ${session.attempt}`;
    expect(session.audioTrackIds(), `${label} audio tracks`).to.have.members(ids);
    for (const trackId of ids) {
        expect(packets.some(packet => packet.kind === 'audio' && packet.trackId === trackId && packet.sequenceHeader),
            `${label} audio track ${trackId} header`).to.equal(true);
        expect(packets.some(packet => packet.kind === 'audio' && packet.trackId === trackId && packet.packetType === 1),
            `${label} audio track ${trackId} frames`).to.equal(true);
    }
}

/** Asserts actual encoded video frames; sequence headers and end-of-sequence messages do not count. */
export function expectVideoFrames(session: RtmpTestSession): void {
    expect(session.snapshot().some(packet => packet.kind === 'video' && (packet.packetType === 1 || packet.packetType === 3)),
        `RTMP ${session.streamKey}, attempt ${session.attempt} video frames`).to.equal(true);
}
