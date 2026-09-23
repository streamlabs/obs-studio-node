import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import { RtmpTestServer } from '../util/rtmp-test-server';
import { expectAudioTracks, expectVideoFrames } from '../util/rtmp-assertions';
import { createStreamingOutput, ITestStreamingOutput, TStreamingMode } from '../util/streaming_output';

const testName = 'osn-streaming-vod';
const mediaCounts = { audioPackets: 25, videoPackets: 15 };

describe(testName, function () {
    this.timeout(30000);
    let obs: OBSHandler;
    let horizontal: osn.IVideo;
    let vertical: osn.IVideo;
    let receiver: RtmpTestServer;
    let outputs: ITestStreamingOutput[];
    let services: osn.IService[];

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
        services = [];
        receiver = await RtmpTestServer.start();
    });

    afterEach(async () => {
        try {
            for (const output of outputs) await output.destroy();
            services.forEach(service => osn.ServiceFactory.destroy(service));
            receiver.assertHealthy();
        } finally { await receiver.close(); }
    });

    after(() => {
        if (vertical) vertical.destroy();
        if (horizontal) horizontal.destroy();
        if (obs) obs.shutdown();
    });

    function createService(id: 'rtmp_common' | 'rtmp_custom', key: string, settings: osn.ISettings = {}): osn.IService {
        const service = osn.ServiceFactory.create(id, key, settings);
        services.push(service);
        // Preserve the original create-then-merge trigger when the test supplies a stale Twitch label.
        service.update({ server: receiver.url, key, streamType: id });
        return service;
    }

    it('Simple VOD cleanup preserves source routing used by another output', async () => {
        const source = osn.InputFactory.create('ffmpeg_source', 'shared-desktop-audio');
        source.audioMixers = 63;
        osn.Global.setOutputSource(1, source);
        const first = createStreamingOutput({
            mode: 'Simple', video: horizontal, name: 'first-twitch',
            service: createService('rtmp_common', 'first-twitch', { service: 'Twitch' }),
        });
        outputs.push(first);
        const companion = createStreamingOutput({
            mode: 'Simple', video: vertical, name: 'second-twitch',
            service: createService('rtmp_common', 'second-twitch', { service: 'Twitch' }),
        });
        outputs.push(companion);
        first.stream.enableTwitchVOD = true;
        companion.stream.enableTwitchVOD = true;
        try {
            await first.start();
            const session = await receiver.waitForPublish('first-twitch');
            await session.waitForMedia(mediaCounts);
            await companion.start();
            await (await receiver.waitForPublish('second-twitch')).waitForMedia(mediaCounts);
            const mixers = source.audioMixers;
            expect(mixers & 1).to.equal(1);
            expect(mixers & (1 << 5)).to.equal(0);
            await first.stop();
            first.stream.enableTwitchVOD = false;
            await first.start();
            const restarted = await receiver.waitForPublish('first-twitch', { afterAttempt: session.attempt });
            await restarted.waitForMedia(mediaCounts);
            await first.stop();
            expectAudioTracks(restarted, [0]);
            expectVideoFrames(restarted);
            expect(source.audioMixers).to.equal(mixers);
        } finally {
            await first.stop();
            await companion.stop();
            osn.Global.setOutputSource(1, null);
            source.release();
        }
    });

    for (const mode of ['Simple', 'Advanced'] as TStreamingMode[]) {
        for (const inherited of [false, true]) {
            it(`${mode}: custom RTMP sends one track with VOD enabled${inherited ? ' and inherited Twitch settings' : ''}`, async () => {
                const output = createStreamingOutput({
                    mode, video: vertical, name: 'custom',
                    service: createService('rtmp_custom', 'custom', inherited ? { service: 'Twitch' } : {}),
                });
                outputs.push(output);
                output.stream.enableTwitchVOD = true;
                await output.start();
                const session = await receiver.waitForPublish('custom');
                await session.waitForMedia(mediaCounts);
                await output.stop();
                expectAudioTracks(session, [0]);
                expectVideoFrames(session);
            });
        }

        it(`${mode}: retained Twitch output removes and restores VOD when the preference changes`, async () => {
            const output = createStreamingOutput({
                mode, video: horizontal, name: 'twitch',
                service: createService('rtmp_common', 'twitch', { service: 'Twitch' }),
            });
            outputs.push(output);
            let previousAttempt = 0;
            for (const enabled of [true, false, true, false]) {
                output.stream.enableTwitchVOD = enabled;
                await output.start();
                const session = await receiver.waitForPublish('twitch', { afterAttempt: previousAttempt });
                await session.waitForMedia(mediaCounts);
                await output.stop();
                expectAudioTracks(session, enabled ? [0, 1] : [0]);
                expectVideoFrames(session);
                previousAttempt = session.attempt;
            }
        });

        it(`${mode}: retained output clears VOD after switching from Twitch to custom RTMP`, async () => {
            const output = createStreamingOutput({
                mode, video: horizontal, name: 'switch-provider',
                service: createService('rtmp_common', 'switch-provider', { service: 'Twitch' }),
            });
            outputs.push(output);
            output.stream.enableTwitchVOD = true;
            await output.start();
            const twitch = await receiver.waitForPublish('switch-provider');
            await twitch.waitForMedia(mediaCounts);
            await output.stop();
            expectAudioTracks(twitch, [0, 1]);
            expectVideoFrames(twitch);
            output.stream.service = createService('rtmp_custom', 'switch-provider', { service: 'Twitch' });
            await output.start();
            const custom = await receiver.waitForPublish('switch-provider', { afterAttempt: twitch.attempt });
            await custom.waitForMedia(mediaCounts);
            await output.stop();
            expectAudioTracks(custom, [0]);
            expectVideoFrames(custom);
        });

        for (const swapped of [false, true]) {
            it(`${mode}: simultaneous Twitch and custom outputs isolate VOD (${swapped ? 'Twitch vertical' : 'Twitch horizontal'})`, async () => {
                const twitch = createStreamingOutput({
                    mode, video: swapped ? vertical : horizontal, name: 'twitch',
                    service: createService('rtmp_common', 'twitch', { service: 'Twitch' }),
                });
                outputs.push(twitch);
                const custom = createStreamingOutput({
                    mode, video: swapped ? horizontal : vertical, name: 'custom',
                    service: createService('rtmp_custom', 'custom', { service: 'Twitch' }),
                });
                outputs.push(custom);
                twitch.stream.enableTwitchVOD = true;
                custom.stream.enableTwitchVOD = true;
                await twitch.start();
                const twitchSession = await receiver.waitForPublish('twitch');
                await twitchSession.waitForMedia(mediaCounts);
                await custom.start();
                const customSession = await receiver.waitForPublish('custom');
                await customSession.waitForMedia(mediaCounts);
                await custom.stop();
                await twitch.stop();
                expectAudioTracks(twitchSession, [0, 1]);
                expectVideoFrames(twitchSession);
                expectAudioTracks(customSession, [0]);
                expectVideoFrames(customSession);
            });
        }
    }
});
