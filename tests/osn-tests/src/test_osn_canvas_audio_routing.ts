import 'mocha';
import { expect } from 'chai';
import { randomUUID } from 'crypto';
import { execFileSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import { logInfo } from '../util/logger';
import { RtmpTestServer, RtmpTestSession } from '../util/rtmp-test-server';
import { expectAudioTracks, expectVideoFrames, getAacAudioTrack } from '../util/rtmp-assertions';
import { getMeanVolumeDb } from '../util/media_probe';
import { createStreamingOutput, ITestStreamingOutput, TStreamingMode } from '../util/streaming_output';

const testName = 'osn-canvas-audio-routing';
const settlingMs = 1000;
const windowMs = 500;
const toneDb = 20 * Math.log10(12000 / 32768 / Math.sqrt(2));

describe(testName, function () {
    this.timeout(60000);
    let obs: OBSHandler;
    let horizontal: osn.IVideo;
    let vertical: osn.IVideo;
    let directory: string;
    let toneFile: string;
    let ffmpeg: string;
    let previousMultipleRendering: boolean;
    let receiver: RtmpTestServer;
    let outputs: ITestStreamingOutput[];
    let services: osn.IService[];
    let scenes: osn.IScene[];
    let sceneSources: osn.IInput[];
    let items: osn.ISceneItem[];
    let source: osn.IInput;
    let sessions: RtmpTestSession[];
    let caseName: string;
    let captureIndex = 0;
    let unsafeCleanup = false;
    const measurements: object[] = [];

    before(() => {
        deleteConfigFiles();
        obs = new OBSHandler(testName, false);
        previousMultipleRendering = osn.Global.multipleRendering;
        directory = fs.mkdtempSync(path.join(__dirname, '..', 'osnData', `${testName}-`));
        toneFile = path.join(directory, 'tone.wav');
        const executable = process.platform === 'win32' ? 'ffmpeg.exe' : 'ffmpeg';
        ffmpeg = [process.env.FFMPEG_PATH, path.join(osn.wd, executable), path.join(osn.wd, 'Frameworks', executable)]
            .find(candidate => candidate && fs.existsSync(candidate)) || executable;
        const rate = 48000;
        const pcm = Buffer.alloc(rate * 2);
        for (let i = 0; i < rate; i++) pcm.writeInt16LE(Math.round(12000 * Math.sin(2 * Math.PI * 440 * i / rate)), i * 2);
        const header = Buffer.alloc(44);
        header.write('RIFF'); header.writeUInt32LE(pcm.length + 36, 4); header.write('WAVEfmt ', 8);
        header.writeUInt32LE(16, 16); header.writeUInt16LE(1, 20); header.writeUInt16LE(1, 22);
        header.writeUInt32LE(rate, 24); header.writeUInt32LE(rate * 2, 28);
        header.writeUInt16LE(2, 32); header.writeUInt16LE(16, 34); header.write('data', 36); header.writeUInt32LE(pcm.length, 40);
        fs.writeFileSync(toneFile, Buffer.concat([header, pcm]));
        const videoInfo = {
            fpsNum: 30, fpsDen: 1, baseWidth: 320, baseHeight: 180, outputWidth: 320, outputHeight: 180,
            outputFormat: osn.EVideoFormat.NV12, colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial, scaleType: osn.EScaleType.Bilinear, fpsType: osn.EFPSType.Fractional,
        };
        horizontal = osn.VideoFactory.create(); horizontal.video = videoInfo;
        vertical = osn.VideoFactory.create();
        vertical.video = { ...videoInfo, baseWidth: 180, baseHeight: 320, outputWidth: 180, outputHeight: 320 };
        for (const index of [1, 2]) osn.AudioTrackFactory.setAtIndex(osn.AudioTrackFactory.create(160, `${testName}-${index}`), index);
        logInfo(testName, `Retaining audio windows in ${directory}`);
    });

    beforeEach(async function () {
        expect(unsafeCleanup, 'A preceding output did not stop safely').to.equal(false);
        caseName = this.currentTest.title;
        outputs = []; services = []; scenes = []; sceneSources = []; items = []; sessions = [];
        source = undefined;
        receiver = undefined;
        receiver = await RtmpTestServer.start();
        source = osn.InputFactory.create('ffmpeg_source', `canvas-tone-${randomUUID()}`, {
            is_local_file: true, local_file: toneFile, looping: true,
            restart_on_activate: false, close_when_inactive: false,
        });
        source.monitoringType = osn.EMonitoringType.None;
        source.audioMixers = 1;
        expect(source.monitoringType).to.equal(osn.EMonitoringType.None);
    });

    afterEach(async () => {
        let failure: Error;
        for (const output of outputs) {
            try { await output.destroy(); }
            catch (error) { unsafeCleanup = true; failure = failure || error; }
        }
        try {
            if (!unsafeCleanup) {
                services.forEach(service => osn.ServiceFactory.destroy(service));
                osn.Global.setOutputSource(0, null);
                osn.Global.setOutputSource(1, null);
                items.slice().reverse().forEach(item => item.remove());
                sceneSources.forEach(input => input.release());
                scenes.slice().reverse().forEach(scene => scene.release());
                if (source) source.release();
                osn.Global.multipleRendering = previousMultipleRendering;
            }
            if (receiver) receiver.assertHealthy();
        } catch (error) { failure = failure || error; }
        finally { if (receiver) await receiver.close(); }
        if (failure) throw failure;
    });

    after(() => {
        try {
            if (!unsafeCleanup) {
                if (vertical) vertical.destroy();
                if (horizontal) horizontal.destroy();
            }
        } finally {
            try { if (obs) obs.shutdown(); }
            finally {
                // The setter stops client signal workers without calling IPC on outputs retained after a stop timeout.
                if (unsafeCleanup) outputs.forEach(output => { output.stream.signalHandler = () => {}; });
            }
        }
    });

    function scene(): osn.IScene {
        const result = osn.SceneFactory.create(`canvas-scene-${randomUUID()}`);
        scenes.push(result);
        return result;
    }

    function add(parent: osn.IScene, child: osn.IInput | osn.IScene, canvas?: osn.IVideo): osn.ISceneItem {
        const input = 'source' in child ? child.source : child;
        if ('source' in child) sceneSources.push(input);
        const item = parent.add(input);
        items.push(item);
        if (canvas) item.video = canvas;
        item.visible = true;
        return item;
    }

    async function start(mode: TStreamingMode, root: osn.IScene, verticalTrack = 1) {
        osn.Global.setOutputSource(0, root);
        source.restart();
        for (const [name, canvas] of [['horizontal', horizontal], ['vertical', vertical]] as [string, osn.IVideo][]) {
            const service = osn.ServiceFactory.create('rtmp_custom', `canvas-service-${randomUUID()}`, {
                server: receiver.url, key: name,
            });
            services.push(service);
            const output = createStreamingOutput({ mode, name, video: canvas, service });
            outputs.push(output);
            if (mode === 'Advanced' && name === 'vertical') (output.stream as osn.IAdvancedStreaming).audioTrack = verticalTrack;
            await output.start();
            sessions.push(await receiver.waitForPublish(name));
        }
        await Promise.all(sessions.map(session => session.waitForMedia({ audioPackets: 25, videoPackets: 15 })));
    }

    async function measure(label: string): Promise<number[]> {
        const index = ++captureIndex;
        const values = await Promise.all(sessions.map(async session => {
            const audioFrames = () => session.snapshot().filter(packet => packet.kind === 'audio' && packet.packetType === 1);
            const initial = audioFrames();
            const begin = initial[initial.length - 1].timestamp + settlingMs;
            const end = begin + windowMs;
            const deadline = Date.now() + 10000;
            let frames = initial;
            while (frames[frames.length - 1].timestamp < end) {
                const remaining = end - frames[frames.length - 1].timestamp;
                await session.waitForMedia({ audioPackets: frames.length + Math.max(1, Math.floor(remaining / 24)) }, Math.max(1, deadline - Date.now()));
                frames = audioFrames();
                if (Date.now() >= deadline && frames[frames.length - 1].timestamp < end) throw new Error(`Timed out capturing ${label}`);
            }
            expectAudioTracks(session, [0]); expectVideoFrames(session);
            const packets = session.snapshot().filter(packet => packet.kind === 'audio' &&
                (packet.sequenceHeader || (packet.timestamp >= begin && packet.timestamp < end)));
            expect(packets.filter(packet => packet.packetType === 1).length, `${label} ${session.streamKey} sample window`).to.be.greaterThan(15);
            // Keep the original AAC configuration while decoding only this phase's settled timestamp window.
            const window = Object.create(session) as RtmpTestSession;
            window.snapshot = () => packets;
            const file = path.join(directory, `${index}-${label}-${session.streamKey}.aac`);
            fs.writeFileSync(file, getAacAudioTrack(window, 0));
            const decoded = file.replace(/\.aac$/, '.wav');
            execFileSync(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-nostdin', '-i', file, '-c:a', 'pcm_s16le', decoded],
                { timeout: 10000, windowsHide: true });
            const db = getMeanVolumeDb(decoded);
            measurements.push({ case: caseName, label, output: session.streamKey, begin, end, db, file, decoded });
            return db;
        }));
        fs.writeFileSync(path.join(directory, 'measurements.json'), JSON.stringify(measurements, null, 2));
        logInfo(testName, `${caseName}: ${label}: horizontal ${values[0]} dBFS, vertical ${values[1]} dBFS`);
        return values;
    }

    function check(values: number[], expected: boolean[], reference: number[], label: string) {
        values.forEach((db, index) => {
            const message = `${label} ${index === 0 ? 'horizontal' : 'vertical'} audio`;
            if (expected[index]) {
                expect(db, `${message} is audible`).to.be.greaterThan(-50);
                expect(db, `${message} preserves single-source gain`).to.be.closeTo(reference[index], 1);
            } else expect(db, `${message} is silent`).to.be.lessThan(-80);
        });
    }

    async function baseline(label: string): Promise<number[]> {
        const values = await measure(label);
        values.forEach(db => expect(db, 'Generated tone has single-source gain').to.be.closeTo(toneDb, 1));
        return values;
    }

    for (const mode of ['Simple', 'Advanced'] as TStreamingMode[]) {
        for (const selective of [false, true]) {
            const configuration = `${mode}, selective ${selective ? 'on' : 'off'}`;
            it(`${configuration}: live visibility isolates canvases and preserves global roots`, async () => {
                osn.Global.multipleRendering = selective;
                const root = scene();
                const h = add(root, source, horizontal);
                const v = add(root, source, vertical);
                await start(mode, root);
                const reference = await baseline('both-visible');
                check(reference, [true, true], reference, 'baseline');
                v.visible = false;
                check(await measure('vertical-hidden'), [true, false], reference, 'vertical hidden');
                h.visible = false; v.visible = true;
                check(await measure('horizontal-hidden'), [false, true], reference, 'horizontal hidden');
                v.visible = false;
                check(await measure('both-hidden'), [false, false], reference, 'both hidden');
                h.visible = true; v.visible = true;
                check(await measure('restored'), [true, true], reference, 'visibility restored');
                h.visible = false; v.visible = false;
                osn.Global.setOutputSource(1, source);
                check(await measure('global-root'), [true, true], reference, 'global root');
                osn.Global.setOutputSource(1, null);
                check(await measure('global-root-cleared'), [false, false], reference, 'global root cleared');
            });

            it(`${configuration}: duplicate nested paths retain gain and ancestor restrictions`, async () => {
                osn.Global.multipleRendering = selective;
                const root = scene();
                const nested = scene();
                add(nested, source);
                const h = add(root, nested, horizontal);
                const v = add(root, nested, vertical);
                await start(mode, root);
                const reference = await baseline('nested-baseline');
                check(reference, [true, true], reference, 'nested baseline');
                const alternate = add(root, nested, horizontal);
                check(await measure('nested-duplicate'), [true, true], reference, 'duplicate path');
                h.visible = false;
                check(await measure('alternate-path'), [true, true], reference, 'alternate visible path');
                alternate.visible = false;
                check(await measure('horizontal-parents-hidden'), [false, true], reference, 'hidden horizontal parents');
                v.visible = false;
                check(await measure('all-parents-hidden'), [false, false], reference, 'all parents hidden');
                const mismatched = scene();
                add(mismatched, source, vertical);
                const restricted = add(root, mismatched, horizontal);
                check(await measure('incompatible-canvases'), [false, false], reference, 'ancestor and leaf canvas intersection');
                restricted.visible = false;
                h.visible = true;
                check(await measure('null-child-canvas'), [true, false], reference, 'unassigned child inherits parent eligibility');
                add(root, source);
                check(await measure('null-root-canvas'), [true, true], reference, 'unassigned root item reaches both canvases');
            });
        }
    }

    for (const selective of [false, true]) {
        it(`Advanced, selective ${selective ? 'on' : 'off'}: source mixer changes isolate live output tracks`, async () => {
            osn.Global.multipleRendering = selective;
            const root = scene();
            add(root, source, horizontal); add(root, source, vertical);
            source.audioMixers = 3;
            await start('Advanced', root, 2);
            const reference = await baseline('tracks-baseline');
            check(reference, [true, true], reference, 'track baseline');
            source.audioMixers = 1;
            check(await measure('track-one-only'), [true, false], reference, 'track one');
            source.audioMixers = 2;
            check(await measure('track-two-only'), [false, true], reference, 'track two');
            source.audioMixers = 0;
            check(await measure('no-tracks'), [false, false], reference, 'all tracks excluded');
        });
    }
});
