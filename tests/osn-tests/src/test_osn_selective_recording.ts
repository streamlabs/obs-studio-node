import 'mocha';
import { expect } from 'chai';
import { randomUUID } from 'crypto';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';
import { getVideoFrameRgb } from '../util/media_probe';

const testName = 'osn-selective-recording';
const width = 320;
const height = 180;
type RGB = [number, number, number];
const background: RGB = [0, 255, 0];
const markers = [
    { name: 'stream-only', x: 16, y: 16, color: 0xffffffff, rgb: [255, 255, 255] as RGB, stream: true, record: false },
    { name: 'recording-only', x: 104, y: 16, color: 0xff0000ff, rgb: [255, 0, 0] as RGB, stream: false, record: true },
    { name: 'both', x: 192, y: 16, color: 0xffff0000, rgb: [0, 0, 255] as RGB, stream: true, record: true },
    { name: 'neither', x: 16, y: 104, color: 0xff00ffff, rgb: [255, 255, 0] as RGB, stream: false, record: false },
];

describe(testName, function () {
    this.timeout(30000);
    let obs: OBSHandler;
    let video: osn.IVideo;
    let scene: osn.IScene;
    let directory: string;
    let previousMultipleRendering: boolean;
    const inputs: osn.IInput[] = [];
    const items: osn.ISceneItem[] = [];
    // Retain timed-out outputs until runtime shutdown rather than freeing active encoders.
    const pendingOutputs: (() => void)[] = [];

    before(() => {
        deleteConfigFiles();
        obs = new OBSHandler(testName, false);
        previousMultipleRendering = osn.Global.multipleRendering;
        directory = fs.mkdtempSync(path.join(os.tmpdir(), `${testName}-`));
        video = osn.VideoFactory.create();
        video.video = {
            fpsNum: 30, fpsDen: 1, baseWidth: width, baseHeight: height,
            outputWidth: width, outputHeight: height, outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709, range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Bilinear, fpsType: osn.EFPSType.Fractional,
        };
        osn.AudioTrackFactory.setAtIndex(osn.AudioTrackFactory.create(160, testName), 1);
        scene = osn.SceneFactory.create(testName);
        function addBlock(name: string, x: number, y: number, color: number,
            stream: boolean, record: boolean, blockWidth = 64, blockHeight = 64) {
            const input = osn.InputFactory.create('color_source', `${testName}-${name}`, {
                width: blockWidth, height: blockHeight, color,
            });
            inputs.push(input);
            const item = scene.add(input);
            items.push(item);
            item.video = video;
            item.position = { x, y };
            item.visible = true;
            item.streamVisible = stream;
            item.recordingVisible = record;
        }
        addBlock('background', 0, 0, 0xff00ff00, true, true, width, height);
        markers.forEach(marker => addBlock(marker.name, marker.x, marker.y, marker.color, marker.stream, marker.record));
        osn.Global.setOutputSource(0, scene);
    });

    beforeEach(() => {
        expect(pendingOutputs.length).to.equal(0, 'A preceding output failed to stop safely');
    });

    after(() => {
        try {
            if (pendingOutputs.length === 0 && obs) {
                osn.Global.multipleRendering = previousMultipleRendering;
                osn.Global.setOutputSource(0, null);
                items.forEach(item => item.remove());
                if (scene) scene.release();
                inputs.forEach(input => input.release());
                if (video) video.destroy();
            }
        } finally {
            if (obs) obs.shutdown();
            // Factory destruction also releases client-side signal workers after disconnect.
            pendingOutputs.forEach(destroy => destroy());
            if (directory) fs.rmSync(directory, { recursive: true, force: true });
        }
    });

    async function record(quality?: osn.ERecordingQuality): Promise<string> {
        const recording = quality === undefined ? osn.AdvancedRecordingFactory.create() : osn.SimpleRecordingFactory.create();
        let encoder: osn.IVideoEncoder;
        let audio: osn.IAudioEncoder;
        let started = false;
        let captureActive = false;
        const signals: osn.EOutputSignal[] = [];
        const name = `${testName}-${randomUUID()}`;
        const hasSignal = (name: EOBSOutputSignal) => signals.some(signal =>
            signal.type === EOBSOutputType.Recording && signal.signal === name);

        async function waitFor(names: EOBSOutputSignal[], rejectFailure = true) {
            const deadline = Date.now() + 10000;
            while (Date.now() < deadline) {
                const failure = signals.find(signal =>
                    (signal.signal === EOBSOutputSignal.Stop && signal.code !== osn.EOutputCode.Success) ||
                    signal.signal === EOBSOutputSignal.WriteError);
                if (rejectFailure && failure) throw new Error(`Recording failed: ${JSON.stringify(failure)}`);
                if (names.every(hasSignal) && (!names.includes(EOBSOutputSignal.Stop) || !captureActive)) return;
                await sleep(25);
            }
            throw new Error(`Recording timed out waiting for ${names.join(', ')}: ${JSON.stringify(signals)}`);
        }

        function destroyOutput() {
            if (quality === undefined) osn.AdvancedRecordingFactory.destroy(recording as osn.IAdvancedRecording);
            else osn.SimpleRecordingFactory.destroy(recording as osn.ISimpleRecording);
        }

        try {
            encoder = osn.VideoEncoderFactory.create('obs_x264', name, {
                rate_control: 'CRF', crf: 23, preset: 'ultrafast', keyint_sec: 1,
            });
            recording.video = video;
            recording.videoEncoder = encoder;
            recording.path = directory;
            recording.format = osn.ERecordingFormat.MP4;
            recording.fileFormat = name;
            if (quality === undefined) {
                (recording as osn.IAdvancedRecording).useStreamEncoders = false;
                (recording as osn.IAdvancedRecording).mixer = 1;
            } else {
                (recording as osn.ISimpleRecording).quality = quality;
                audio = osn.AudioEncoderFactory.create('ffmpeg_aac', `${name}-audio`);
                (recording as osn.ISimpleRecording).audioEncoder = audio;
            }
            recording.signalHandler = signal => {
                signals.push(signal);
                if (signal.signal === EOBSOutputSignal.Activate) captureActive = true;
                if (signal.signal === EOBSOutputSignal.Deactivate) captureActive = false;
            };
            recording.start();
            started = true;
            await waitFor([EOBSOutputSignal.Start]);
            await sleep(2000);
            recording.stop();
            await waitFor([EOBSOutputSignal.Stop, EOBSOutputSignal.Wrote]);
            started = false;
            return recording.lastFile();
        } finally {
            try {
                if (started) {
                    if (!hasSignal(EOBSOutputSignal.Stop)) recording.stop(true);
                    await waitFor([EOBSOutputSignal.Stop], false);
                    started = false;
                }
            } finally {
                if (started) pendingOutputs.push(destroyOutput);
                else {
                    destroyOutput();
                    if (audio) audio.release();
                    if (encoder) encoder.release();
                }
            }
        }
    }

    function expectRegion(frame: Buffer, x: number, y: number, expected: RGB, description: string) {
        const sum: RGB = [0, 0, 0];
        // Sample the interior of each block, away from compression and chroma edges.
        for (let dy = 0; dy < 8; dy++) {
            for (let dx = 0; dx < 8; dx++) {
                const offset = ((y + dy) * width + x + dx) * 3;
                for (let channel = 0; channel < 3; channel++) sum[channel] += frame[offset + channel];
            }
        }
        const actual = sum.map(value => Math.round(value / 64));
        expected.forEach((value, channel) => {
            expect(actual[channel], `${description}: expected ${expected}, got ${actual}`).to.be.closeTo(value, 25);
        });
    }

    async function checkRecording(selective: boolean, quality?: osn.ERecordingQuality) {
        // Set the rendering mode before output/encoder creation. No replay or streaming is involved.
        osn.Global.multipleRendering = selective;
        const file = await record(quality);
        for (const time of [0.5, 1, 1.5]) {
            const frame = getVideoFrameRgb(file, time, width, height);
            expectRegion(frame, 288, 144, background, `Background control at ${time}s`);
            markers.forEach(marker => expectRegion(frame, marker.x + 24, marker.y + 24,
                selective && !marker.record ? background : marker.rgb, `${marker.name} at ${time}s`));
        }
    }

    for (const [name, quality] of [
        ['HighQuality', osn.ERecordingQuality.HighQuality],
        ['HigherQuality', osn.ERecordingQuality.HigherQuality],
    ] as [string, osn.ERecordingQuality][]) {
        for (const selective of [false, true]) {
            it(`Simple ${name}: ${selective ? 'honors recording visibility' : 'ignores selective masks when disabled'}`, async () => {
                await checkRecording(selective, quality);
            });
        }
    }

    it('Advanced: honors recording visibility with a dedicated encoder', async () => {
        await checkRecording(true);
    });
});
