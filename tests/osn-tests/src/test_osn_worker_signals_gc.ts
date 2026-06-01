import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';
import { ERecordingFormat } from '../osn';
import path = require('path');

declare const global: { gc?: () => void };

const testName = 'osn-worker-signals-gc';

// Regression coverage for the WorkerSignals destructor fix:
// when an output's JS wrapper is GC'd without Factory.destroy(), the
// signal-poll worker thread must be stopped/joined cleanly by the
// destructor instead of being left calling BlockingCall on a torn-down TSFN.
describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    before(async () => {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);
        obs.instantiateUserPool(testName);
        await obs.reserveUser();
    });

    after(async function () {
        await obs.releaseUser();
        obs.shutdown();

        if (hasTestFailed === true) {
            logInfo(testName, 'One or more test cases failed. Uploading cache');
            await obs.uploadTestCache();
        }

        obs = null;
        deleteConfigFiles();
        logInfo(testName, 'Finished ' + testName + ' tests');
        logEmptyLine();
    });

    afterEach(async function () {
        hasTestFailed = (await obs.finalizeRetryableTest(this)) || hasTestFailed;
    });

    async function dropAndCollect() {
        expect(global.gc, '--expose-gc must be enabled for this test').to.be.a('function');
        // Two passes with a yield between catches wrappers promoted out of new-space.
        global.gc!();
        await sleep(200);
        global.gc!();
        await sleep(500);
    }

    it('Drops a running SimpleStreaming without destroy() and finalizes cleanly', async function () {
        if (obs.isDarwin()) {
            this.skip();
        }

        let stream: osn.ISimpleStreaming | null = osn.SimpleStreamingFactory.create();
        stream.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-gc-simple-streaming');
        stream.service = osn.ServiceFactory.legacySettings;
        stream.delay = osn.DelayFactory.create();
        stream.reconnect = osn.ReconnectFactory.create();
        stream.network = osn.NetworkFactory.create();
        stream.video = obs.defaultVideoContext;
        stream.audioEncoder =
            osn.AudioEncoderFactory.create('ffmpeg_aac', 'audio-encoder-gc-simple-streaming');
        stream.signalHandler = (signal) => { obs.signals.push(signal); };

        stream.start();

        await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Starting);
        const activateInfo = await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Activate);
        if (activateInfo.signal === EOBSOutputSignal.Stop) {
            throw Error('Stream did not start: code=' + activateInfo.code + ' error=' + activateInfo.error);
        }
        await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Start);

        stream.stop();
        await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stopping);
        await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Stop);
        await obs.getNextSignalInfo(EOBSOutputType.Streaming, EOBSOutputSignal.Deactivate);

        // Intentionally do NOT call SimpleStreamingFactory.destroy(stream).
        // The worker is still polling at this point; GC + finalizer must
        // tear it down without hanging or use-after-free on the TSFN.
        stream = null;
        await dropAndCollect();

        // Process must remain responsive: a follow-up IPC call should succeed.
        const probe = osn.SimpleStreamingFactory.create();
        expect(probe).to.not.equal(undefined);
        osn.SimpleStreamingFactory.destroy(probe);
    });

    it('Drops a running AdvancedRecording without destroy() and finalizes cleanly', async function () {
        if (obs.isDarwin()) {
            this.skip();
        }

        let recording: osn.IAdvancedRecording | null = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-gc-adv-recording');
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track = osn.AudioTrackFactory.create(160, 'gc-track');
        osn.AudioTrackFactory.setAtIndex(track, 1);
        recording.signalHandler = (signal) => { obs.signals.push(signal); };

        recording.start();

        const startInfo = await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Start);
        if (startInfo.signal === EOBSOutputSignal.Stop) {
            throw Error('Recording did not start: code=' + startInfo.code + ' error=' + startInfo.error);
        }

        await sleep(500);

        recording.stop();
        await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Stopping);
        await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Stop);
        await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Wrote);

        recording = null;
        await dropAndCollect();

        const probe = osn.AdvancedRecordingFactory.create();
        expect(probe).to.not.equal(undefined);
        osn.AdvancedRecordingFactory.destroy(probe);
    });

    it('Drops a running AdvancedReplayBuffer without destroy() and finalizes cleanly', async function () {
        if (obs.isDarwin() && obs.isCI()) {
            this.skip();
        }

        let replayBuffer: osn.IAdvancedReplayBuffer | null = osn.AdvancedReplayBufferFactory.create();
        replayBuffer.path = path.join(path.normalize(__dirname), '..', 'osnData');
        replayBuffer.format = osn.ERecordingFormat.MP4;
        replayBuffer.overwrite = false;
        replayBuffer.noSpace = false;
        replayBuffer.video = obs.defaultVideoContext;
        replayBuffer.duration = 10;
        replayBuffer.prefix = 'GC';
        replayBuffer.suffix = 'Test';
        replayBuffer.signalHandler = (signal) => { obs.signals.push(signal); };

        const recording = osn.AdvancedRecordingFactory.create();
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = osn.ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.video = obs.defaultVideoContext;
        recording.videoEncoder =
            osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-gc-adv-replay');
        const track = osn.AudioTrackFactory.create(160, 'gc-replay-track');
        osn.AudioTrackFactory.setAtIndex(track, 1);
        recording.overwrite = false;
        recording.noSpace = false;

        replayBuffer.recording = recording;

        replayBuffer.start();

        const startInfo = await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Start);
        if (startInfo.signal === EOBSOutputSignal.Stop) {
            throw Error('ReplayBuffer did not start: code=' + startInfo.code + ' error=' + startInfo.error);
        }

        await sleep(500);

        replayBuffer.stop();
        await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stopping);
        await obs.getNextSignalInfo(EOBSOutputType.ReplayBuffer, EOBSOutputSignal.Stop);

        // Drop replayBuffer first; recording stays referenced (and is cleaned
        // up below) so we isolate the replay-buffer wrapper's finalizer path.
        replayBuffer = null;
        await dropAndCollect();

        const probe = osn.AdvancedReplayBufferFactory.create();
        expect(probe).to.not.equal(undefined);
        osn.AdvancedReplayBufferFactory.destroy(probe);

        const videoEncoder = recording.videoEncoder;
        osn.AdvancedRecordingFactory.destroy(recording);
        videoEncoder.release();
    });
});
