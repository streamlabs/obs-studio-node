import 'mocha';
import { expect } from 'chai';
import { EventEmitter } from 'events';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import { RtmpTestServer, RtmpTestSession } from '../util/rtmp-test-server';

const testName = 'osn-streaming-vod';
type TMode = 'Simple' | 'Advanced';
interface ITestStreamingOutput {
    stream: osn.ISimpleStreaming | osn.IAdvancedStreaming;
    setService(twitch: boolean, inherited?: boolean): void;
    capture(afterAttempt?: number): Promise<RtmpTestSession>;
    stop(): Promise<void>;
    destroy(): Promise<void>;
}

describe(testName, function () {
    this.timeout(30000);
    let obs: OBSHandler;
    let horizontal: osn.IVideo;
    let vertical: osn.IVideo;
    let receiver: RtmpTestServer;
    let outputs: ITestStreamingOutput[];

    before(() => {
        deleteConfigFiles();
        obs = new OBSHandler(testName, false);
        const videoInfo = {
            fpsNum: 30, fpsDen: 1, baseWidth: 320, baseHeight: 180, outputWidth: 320, outputHeight: 180,
            outputFormat: osn.EVideoFormat.NV12, colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial, scaleType: osn.EScaleType.Bilinear, fpsType: osn.EFPSType.Fractional,
        };
        horizontal = osn.VideoFactory.create();
        horizontal.video = videoInfo;
        vertical = osn.VideoFactory.create();
        vertical.video = { ...videoInfo, baseWidth: 180, baseHeight: 320, outputWidth: 180, outputHeight: 320 };
        for (const index of [1, 2]) osn.AudioTrackFactory.setAtIndex(osn.AudioTrackFactory.create(160, `Track ${index}`), index);
    });

    beforeEach(async () => {
        outputs = [];
        receiver = await RtmpTestServer.start();
    });

    afterEach(async () => {
        try {
            for (const output of outputs) await output.destroy();
            receiver.assertHealthy();
        } finally { await receiver.close(); }
    });

    after(() => {
        if (vertical) vertical.destroy();
        if (horizontal) horizontal.destroy();
        if (obs) obs.shutdown();
    });

    function createOutput(mode: TMode, video: osn.IVideo, key: string, commonTwitch = false, inheritedTwitch = false): ITestStreamingOutput {
        const stream = mode === 'Advanced' ? osn.AdvancedStreamingFactory.create() : osn.SimpleStreamingFactory.create();
        const encoder = osn.VideoEncoderFactory.create('obs_x264', key, {
            bitrate: 500, keyint_sec: 2, preset: 'ultrafast', rate_control: 'CBR',
        });
        const services: osn.IService[] = [];
        let audio: osn.IAudioEncoder;
        let signals: osn.EOutputSignal[] = [];
        let started = false;
        let activated = false;
        const events = new EventEmitter();
        stream.video = video;
        stream.videoEncoder = encoder;
        stream.enforceServiceBitrate = false;
        stream.enableTwitchVOD = true;
        stream.delay = osn.DelayFactory.create();
        stream.delay.enabled = false;
        stream.reconnect = osn.ReconnectFactory.create();
        stream.reconnect.enabled = false;
        stream.network = osn.NetworkFactory.create();
        if (mode === 'Advanced') {
            const advanced = stream as osn.IAdvancedStreaming;
            advanced.audioTrack = 1;
            advanced.twitchTrack = 2;
        } else {
            audio = osn.AudioEncoderFactory.create('ffmpeg_aac', `${key}-audio`);
            audio.bitrate = 160;
            (stream as osn.ISimpleStreaming).audioEncoder = audio;
        }
        stream.signalHandler = signal => {
            signals.push(signal);
            if (signal.signal === 'activate') activated = true;
            events.emit('signal');
        };

        function setService(twitch: boolean, inherited = false) {
            const service = osn.ServiceFactory.create(twitch ? 'rtmp_common' : 'rtmp_custom', key,
                twitch || inherited ? { service: 'Twitch' } : {});
            // Preserve the original create-then-merge trigger, including a stale service label.
            service.update({ server: receiver.url, key, streamType: twitch ? 'rtmp_common' : 'rtmp_custom' });
            services.push(service);
            stream.service = service;
        }
        setService(commonTwitch, inheritedTwitch);

        function waitForSignals(names: string[]) {
            return new Promise<void>((resolve, reject) => {
                const finish = (error?: Error) => {
                    clearTimeout(timer);
                    events.removeListener('signal', check);
                    if (error) reject(error); else resolve();
                };
                const check = () => {
                    const failed = signals.find(signal => signal.signal === 'stop' && signal.code !== 0);
                    if (failed) return finish(new Error(`Native ${key} failed: ${JSON.stringify(failed)}`));
                    if (names.every(name => signals.some(signal => signal.signal === name))) finish();
                };
                const timer = setTimeout(() => finish(new Error(`Native ${key} timed out waiting for ${names}: ${JSON.stringify(signals)}`)), 10000);
                events.on('signal', check);
                check();
            });
        }

        async function stop() {
            if (!started) return;
            if (!signals.some(signal => signal.signal === 'stop')) stream.stop(true);
            await waitForSignals(activated ? ['stop', 'deactivate'] : ['stop']);
            started = false;
        }

        const output = {
            stream, setService, stop,
            async capture(afterAttempt = 0): Promise<RtmpTestSession> {
                signals = [];
                activated = false;
                started = true;
                stream.start();
                await waitForSignals(['start']);
                const session = await receiver.waitForPublish(key, { afterAttempt });
                await session.waitForMedia({ audioPackets: 25, videoPackets: 15 });
                return session;
            },
            async destroy() {
                await stop();
                stream.signalHandler = () => {};
                if (mode === 'Advanced') osn.AdvancedStreamingFactory.destroy(stream as osn.IAdvancedStreaming);
                else osn.SimpleStreamingFactory.destroy(stream as osn.ISimpleStreaming);
                encoder.release();
                if (audio) audio.release();
                services.forEach(service => osn.ServiceFactory.destroy(service));
            },
        };
        outputs.push(output);
        return output;
    }

    function expectTracks(session: RtmpTestSession, ids: number[]) {
        const packets = session.snapshot();
        expect(session.audioTrackIds()).to.deep.equal(ids);
        expect(packets.some(packet => packet.kind === 'video' && !packet.sequenceHeader)).to.equal(true);
        for (const trackId of ids) {
            expect(packets.some(packet => packet.kind === 'audio' && packet.trackId === trackId && packet.sequenceHeader), `track ${trackId} header`).to.equal(true);
            expect(packets.some(packet => packet.kind === 'audio' && packet.trackId === trackId && packet.packetType === 1), `track ${trackId} frames`).to.equal(true);
        }
    }

    it('Simple VOD cleanup preserves source routing used by another output', async () => {
        const source = osn.InputFactory.create('ffmpeg_source', 'shared-desktop-audio');
        source.audioMixers = 63;
        osn.Global.setOutputSource(1, source);
        const first = createOutput('Simple', horizontal, 'first-twitch', true);
        const companion = createOutput('Simple', vertical, 'second-twitch', true);
        try {
            const session = await first.capture();
            await companion.capture();
            const mixers = source.audioMixers;
            expect(mixers & 1).to.equal(1);
            expect(mixers & (1 << 5)).to.equal(0);
            await first.stop();
            first.stream.enableTwitchVOD = false;
            const restarted = await first.capture(session.attempt);
            await first.stop();
            expectTracks(restarted, [0]);
            expect(source.audioMixers).to.equal(mixers);
        } finally {
            await first.stop();
            await companion.stop();
            osn.Global.setOutputSource(1, null);
            source.release();
        }
    });

    for (const mode of ['Simple', 'Advanced'] as TMode[]) {
        for (const inherited of [false, true]) {
            it(`${mode}: custom RTMP sends one track with VOD enabled${inherited ? ' and inherited Twitch settings' : ''}`, async () => {
                const output = createOutput(mode, vertical, 'custom', false, inherited);
                const session = await output.capture();
                await output.stop();
                expectTracks(session, [0]);
            });
        }

        it(`${mode}: retained Twitch output removes and restores VOD when the preference changes`, async () => {
            const output = createOutput(mode, horizontal, 'twitch', true);
            let previousAttempt = 0;
            for (const enabled of [true, false, true, false]) {
                output.stream.enableTwitchVOD = enabled;
                const session = await output.capture(previousAttempt);
                await output.stop();
                expectTracks(session, enabled ? [0, 1] : [0]);
                previousAttempt = session.attempt;
            }
        });

        it(`${mode}: retained output clears VOD after switching from Twitch to custom RTMP`, async () => {
            const output = createOutput(mode, horizontal, 'switch-provider', true);
            const twitch = await output.capture();
            await output.stop();
            expectTracks(twitch, [0, 1]);
            output.setService(false, true);
            const custom = await output.capture(twitch.attempt);
            await output.stop();
            expectTracks(custom, [0]);
        });

        for (const swapped of [false, true]) {
            it(`${mode}: simultaneous Twitch and custom outputs isolate VOD (${swapped ? 'Twitch vertical' : 'Twitch horizontal'})`, async () => {
                const twitch = createOutput(mode, swapped ? vertical : horizontal, 'twitch', true);
                const custom = createOutput(mode, swapped ? horizontal : vertical, 'custom', false, true);
                const twitchSession = await twitch.capture();
                const customSession = await custom.capture();
                await custom.stop();
                await twitch.stop();
                expectTracks(twitchSession, [0, 1]);
                expectTracks(customSession, [0]);
            });
        }
    }
});
