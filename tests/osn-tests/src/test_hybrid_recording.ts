import 'mocha';
import { expect } from 'chai';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as osn from '../osn';
import { deleteConfigFiles, sleep, waitForFile } from '../util/general';
import { probeMedia } from '../util/media_probe';
import { EOBSOutputSignal, EOBSOutputType, EOBSSettingsCategories } from '../util/obs_enums';
import { OBSHandler } from '../util/obs_handler';

describe('Hybrid recording', function () {
    let obs: OBSHandler;
    let directory: string;

    beforeEach(() => {
        deleteConfigFiles();
        obs = new OBSHandler('hybrid-recording');
        directory = fs.mkdtempSync(path.join(os.tmpdir(), 'osn-hybrid-'));
        const video = obs.defaultVideoContext;
        video.video = {
            ...video.video, fpsNum: 30, fpsDen: 1, baseWidth: 320, baseHeight: 180,
            outputWidth: 320, outputHeight: 180,
        };
    });

    afterEach(() => {
        try {
            obs.shutdown();
        } finally {
            fs.rmSync(directory, { recursive: true, force: true });
        }
    });

    async function waitForFrames(count: number) {
        const target = obs.defaultVideoContext.encodedFrames + count;
        const deadline = Date.now() + 15000;
        while (obs.defaultVideoContext.encodedFrames < target && Date.now() < deadline) {
            await sleep(50);
        }
        expect(obs.defaultVideoContext.encodedFrames, 'Video frames reaching the encoder').to.be.at.least(target);
    }

    function checkFile(file: string, extension: string, hybrid: boolean, bFrames: boolean) {
        const streamFields = 'index,codec_type,codec_name,start_pts,duration_ts,duration,has_b_frames,nb_frames,nb_read_frames';
        const media = probeMedia(file, ['-count_frames', '-show_entries',
            `stream=${streamFields}:packet=stream_index,pts,flags:format_tags`]);
        const tags = media.format.tags;
        expect(tags.major_brand === 'qt  ', file).to.equal(extension === 'mov');
        const writtenByObs = Object.keys(tags).some(key => String(tags[key]).startsWith('OBS Studio ('));
        expect(writtenByObs, `Native OBS muxer metadata in ${file}`).to.equal(hybrid);
        const video = media.streams.find(stream => stream.codec_type === 'video');
        const audio = media.streams.find(stream => stream.codec_type === 'audio');
        expect(video.codec_name).to.equal('h264');
        expect(audio.codec_name).to.equal('aac');
        if (bFrames) expect(video.has_b_frames, file).to.be.greaterThan(0);
        expect(Number(video.nb_read_frames), file).to.be.greaterThan(5);
        const packets = media.packets.filter(packet => packet.stream_index === video.index);
        expect(packets.length, 'All stored video packets can be read').to.equal(Number(video.nb_frames));
        const discarded = packets.filter(packet => packet.flags.includes('D'));
        for (const packet of discarded) {
            expect(packet.pts < video.start_pts || packet.pts >= video.start_pts + video.duration_ts,
                'Only packets outside the presentation interval can be discarded').to.be.true;
        }
        expect(Number(video.nb_read_frames), 'All presentation frames decode').to.equal(packets.length - discarded.length);
        if (discarded.length) {
            // An out-of-range reference picture can still be required to decode the last B-frames.
            const raw = probeMedia(file, ['-ignore_editlist', '1', '-select_streams', 'v:0', '-count_frames',
                '-show_entries', 'stream=nb_read_frames']);
            expect(Number(raw.streams[0].nb_read_frames), 'Every stored video sample decodes').to.equal(packets.length);
        }
        expect(Number(audio.nb_read_frames), file).to.be.greaterThan(5);
        expect(Math.abs(Number(video.duration) - Number(audio.duration)), 'Audio and video end together')
            .to.be.lessThan(0.3);
    }

    async function expectRecordingFinished() {
        const signals = [EOBSOutputSignal.Stop, EOBSOutputSignal.Wrote];
        const first = await obs.getNextSignalInfoOf(EOBSOutputType.Recording, signals);
        expect(first.code, first.error).to.equal(osn.EOutputCode.Success);
        expect(signals).to.include(first.signal);
        const other = signals.find(signal => signal !== first.signal);
        const second = await obs.getNextSignalInfo(EOBSOutputType.Recording, other);
        expect(second.signal, second.error).to.equal(other);
        expect(second.code, second.error).to.equal(osn.EOutputCode.Success);
    }

    async function record(recording: osn.IRecording, format: osn.ERecordingFormat,
        split: osn.ERecSplitType | null, bFrames: boolean) {
        const hybrid = format === osn.ERecordingFormat.HybridMP4 || format === osn.ERecordingFormat.HybridMOV;
        const extension = format === osn.ERecordingFormat.HybridMOV ? 'mov' : 'mp4';
        const filename = `test-${format}`;
        const first = path.join(directory, `${filename}.${extension}`);
        const second = path.join(directory, `${filename}_2.${extension}`);
        recording.format = format;
        recording.fileFormat = filename;
        recording.overwrite = split === osn.ERecSplitType.Manual;
        recording.enableFileSplit = split !== null;
        if (split !== null) {
            recording.splitType = split;
            recording.splitTime = 1;
        }
        recording.start();
        const started = await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Start);
        expect(started.signal, started.error).to.equal(EOBSOutputSignal.Start);
        try {
            await waitForFrames(36);
            if (split !== null) {
                if (split === osn.ERecSplitType.Manual) recording.splitFile();
                await waitForFile(second, 15000);
                await waitForFrames(18);
            }
        } finally {
            recording.stop();
            await expectRecordingFinished();
        }
        expect(path.normalize(recording.lastFile())).to.equal(split !== null ? second : first);
        checkFile(first, extension, hybrid, bFrames);
        if (split !== null) checkFile(second, extension, hybrid, bFrames);
    }

    function listedRecordingFormats(mode: string): string[] {
        const output = EOBSSettingsCategories.Output;
        obs.setSetting(output, 'Mode', mode);
        const group = obs.getSettingsContainer(output)
            .find(group => group.parameters.some(field => field.name === 'RecFormat'));
        return group.parameters.find(field => field.name === 'RecFormat')
            .values.map(option => option[Object.keys(option)[0]]);
    }

    // CI exports its pinned OBS package independently of the recording capability under test.
    if (process.env.LibOBSVersion === '33.0.1test6') {
        it('Hides and rejects Hybrid recording with the older test6 writer', () => {
            for (const mode of ['Simple', 'Advanced']) {
                const formats = listedRecordingFormats(mode);
                expect(formats).to.include.members(['mp4', 'mov']);
                expect(formats).not.to.include('hybrid_mp4');
                expect(formats).not.to.include('hybrid_mov');
                const simple = mode === 'Simple' ? osn.SimpleRecordingFactory.create() : undefined;
                const advanced = mode === 'Advanced' ? osn.AdvancedRecordingFactory.create() : undefined;
                const recording = simple || advanced;
                try {
                    recording.signalHandler = signal => obs.signals.push(signal);
                    for (const format of [osn.ERecordingFormat.HybridMP4, osn.ERecordingFormat.HybridMOV]) {
                        recording.format = format;
                        expect(() => recording.start()).to.throw(/does not support Hybrid recording/);
                    }
                } finally {
                    if (simple) osn.SimpleRecordingFactory.destroy(simple);
                    else osn.AdvancedRecordingFactory.destroy(advanced);
                }
            }
        });
        return;
    }

    it('Lists and saves the Hybrid formats separately from ordinary MP4 and MOV in both output modes', () => {
        for (const mode of ['Simple', 'Advanced']) {
            const output = EOBSSettingsCategories.Output;
            expect(listedRecordingFormats(mode))
                .to.include.members(['mp4', 'mov', 'hybrid_mp4', 'hybrid_mov']);
            obs.setSetting(output, 'RecFormat', 'mp4');
            obs.setSetting(output, 'RecAEncoder', 'ffmpeg_opus');
            expect(obs.getSetting(output, 'RecAEncoder')).to.equal('ffmpeg_opus');
            for (const format of [osn.ERecordingFormat.HybridMP4, osn.ERecordingFormat.HybridMOV]) {
                obs.setSetting(output, 'RecFormat', format);
                expect(obs.getSetting(output, 'RecFormat')).to.equal(format);
                const expectedAudio = format === osn.ERecordingFormat.HybridMP4 ? 'ffmpeg_opus' : 'ffmpeg_aac';
                expect(obs.getSetting(output, 'RecAEncoder')).to.equal(expectedAudio);
            }
        }
    });

    for (const mode of ['Simple', 'Advanced']) {
        it(`${mode} records the saved Hybrid format through the legacy recording service`, async () => {
            const output = EOBSSettingsCategories.Output;
            const simple = mode === 'Simple';
            obs.connectOutputSignals();
            obs.setSetting(output, 'Mode', mode);
            if (simple) {
                obs.setSetting(output, 'StreamEncoder', obs.os === 'win32' ? 'x264' : 'obs_x264');
                obs.setSetting(output, 'FilePath', directory);
            } else {
                obs.setSetting(output, 'Encoder', 'obs_x264');
                obs.setSetting(output, 'RecEncoder', 'none');
                obs.setSetting(output, 'RecFilePath', directory);
            }
            obs.setSetting(output, 'RecFormat', simple ? 'hybrid_mp4' : 'hybrid_mov');
            osn.NodeObs.OBS_service_startRecording();
            const started = await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Start);
            expect(started.signal, started.error).to.equal(EOBSOutputSignal.Start);
            try {
                await waitForFrames(36);
            } finally {
                osn.NodeObs.OBS_service_stopRecording();
                await expectRecordingFinished();
            }
            const file = path.normalize(osn.NodeObs.OBS_service_getLastRecording());
            const extension = simple ? 'mp4' : 'mov';
            expect(path.dirname(file)).to.equal(directory);
            expect(path.extname(file)).to.equal(`.${extension}`);
            checkFile(file, extension, true, false);
        });

        it(`${mode} switches between Hybrid MP4, Hybrid MOV and ordinary MP4 and finalizes split files`, async () => {
            const simple = mode === 'Simple' ? osn.SimpleRecordingFactory.create() : undefined;
            const advanced = mode === 'Advanced' ? osn.AdvancedRecordingFactory.create() : undefined;
            const recording = simple || advanced;
            const encoder = osn.VideoEncoderFactory.create('obs_x264', 'hybrid-video', {
                preset: 'veryfast', keyint_sec: 2, x264opts: 'bframes=2 b-adapt=0 scenecut=0',
            });
            let audio: osn.IAudioEncoder;
            try {
                recording.video = obs.defaultVideoContext;
                recording.videoEncoder = encoder;
                recording.path = directory;
                recording.noSpace = true;
                recording.signalHandler = signal => obs.signals.push(signal);
                if (simple) {
                    audio = osn.AudioEncoderFactory.create('ffmpeg_aac', 'hybrid-audio');
                    simple.audioEncoder = audio;
                    simple.quality = osn.ERecordingQuality.HighQuality;
                } else {
                    advanced.useStreamEncoders = false;
                    advanced.mixer = 1;
                    osn.AudioTrackFactory.setAtIndex(osn.AudioTrackFactory.create(160, 'hybrid-audio'), 1);
                }
                await record(recording, osn.ERecordingFormat.HybridMP4, simple ? osn.ERecSplitType.Manual : null, !!advanced);
                await record(recording, osn.ERecordingFormat.HybridMOV, advanced ? osn.ERecSplitType.Time : null, !!advanced);
                await record(recording, osn.ERecordingFormat.MP4, null, false);
            } finally {
                if (simple) osn.SimpleRecordingFactory.destroy(simple);
                else osn.AdvancedRecordingFactory.destroy(advanced);
                if (audio) audio.release();
                encoder.release();
            }
        });
    }
});
