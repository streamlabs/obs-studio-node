import { execFileSync, spawnSync } from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as osn from '../osn';

function probeMedia(mediaFile: string, args: string[]): any {
    const executable = process.platform === 'win32' ? 'ffprobe.exe' : 'ffprobe';
    const ffprobe = [
        process.env.FFPROBE_PATH,
        path.join(path.normalize(osn.wd), executable),
        path.join(path.normalize(osn.wd), 'Frameworks', executable),
        path.join(__dirname, '..', '..', '..', 'build', 'libobs-src', 'bin',
            process.arch === 'x64' ? '64bit' : '32bit', executable),
    ].find(candidate => candidate && fs.existsSync(candidate)) || executable;

    return JSON.parse(execFileSync(ffprobe, ['-v', 'error', ...args, '-of', 'json', mediaFile], {
        encoding: 'utf8',
        timeout: 30000,
    }));
}

export function getAudioStreamTitles(mediaFile: string): string[] {
    const probe = probeMedia(mediaFile, ['-select_streams', 'a', '-show_entries', 'stream_tags=title']);
    return (probe.streams || []).map((stream: { tags?: { title?: string } }) => stream.tags?.title || '');
}

export function getAudioStreamBitrates(mediaFile: string): { codec: string, bitrate: number }[] {
    const probe = probeMedia(mediaFile, ['-select_streams', 'a', '-show_entries', 'stream=codec_name,bit_rate']);
    return (probe.streams || []).map((stream: { codec_name: string, bit_rate: string }) => ({
        codec: stream.codec_name,
        bitrate: Number(stream.bit_rate),
    }));
}

export function getVideoKeyframes(mediaFile: string): {
    frameCount: number,
    frameRate: number,
    duration: number,
    times: number[],
} {
    const probe = probeMedia(mediaFile, [
        '-select_streams', 'v:0', '-skip_frame', 'nokey', '-show_entries',
        'frame=pts_time:stream=nb_frames,r_frame_rate,duration',
    ]);
    const stream = probe.streams[0];
    const [numerator, denominator] = stream.r_frame_rate.split('/').map(Number);
    return {
        frameCount: Number(stream.nb_frames),
        frameRate: numerator / denominator,
        duration: Number(stream.duration),
        times: probe.frames.map((frame: { pts_time: string }) => Number(frame.pts_time)),
    };
}

// Prefer an explicit override, then the packaged executable, then PATH.
function resolveFfmpeg(): string {
    const executable = process.platform === 'win32' ? 'ffmpeg.exe' : 'ffmpeg';
    return [
        process.env.FFMPEG_PATH,
        path.join(path.normalize(osn.wd), executable),
        path.join(path.normalize(osn.wd), 'Frameworks', executable),
    ].find(candidate => candidate && fs.existsSync(candidate)) || executable;
}

/** Decodes one unscaled RGB24 frame, checking its decoded width, height and payload length. */
export function getVideoFrameRgb(mediaFile: string, timeSeconds: number, width: number, height: number): Buffer {
    const expectedBytes = width * height * 3;
    const frame = execFileSync(resolveFfmpeg(), [
        '-hide_banner', '-loglevel', 'error', '-nostdin',
        '-ss', String(timeSeconds), '-i', mediaFile, '-map', '0:v:0',
        '-frames:v', '1', '-pix_fmt', 'rgb24', '-c:v', 'ppm', '-f', 'image2pipe', 'pipe:1',
    ], { timeout: 10000, maxBuffer: expectedBytes + 1024, windowsHide: true });

    // FFmpeg emits an 8-bit binary PPM header without comments. Consume exactly
    // one separator after maxval: following whitespace or hash bytes are pixels.
    const header = frame.subarray(0, 1024).toString('ascii').match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
    if (!header) {
        throw new Error(`Missing or invalid RGB24 PPM frame at ${timeSeconds}s in ${mediaFile}`);
    }
    const decodedWidth = Number(header[1]);
    const decodedHeight = Number(header[2]);
    if (decodedWidth !== width || decodedHeight !== height) {
        throw new Error(`Expected a ${width}x${height} RGB frame at ${timeSeconds}s in ${mediaFile}, decoded ${decodedWidth}x${decodedHeight}`);
    }
    const pixels = frame.subarray(header[0].length);
    if (pixels.length !== expectedBytes) {
        throw new Error(`Expected a ${width}x${height} RGB frame at ${timeSeconds}s in ${mediaFile}, got ${pixels.length} bytes`);
    }
    return pixels;
}

// Returns the mean volume (dBFS) of the first audio stream of `mediaFile`, as
// measured by ffmpeg's volumedetect filter. Digital silence reports about
// -91 dB (or -inf); normal program audio sits well above -40 dB, so a threshold
// such as `> -80` cleanly separates "has audio" from "silent".
//
// Throws if ffmpeg cannot be run or the file has no decodable audio stream.
export function getMeanVolumeDb(mediaFile: string): number {
    if (!fs.existsSync(mediaFile)) {
        throw new Error(`getMeanVolumeDb: file does not exist: ${mediaFile}`);
    }

    const ffmpeg = resolveFfmpeg();
    const args = [
        '-hide_banner', '-nostats',
        '-i', mediaFile,
        '-map', '0:a:0',
        '-af', 'volumedetect',
        '-f', 'null', '-',
    ];

    const result = spawnSync(ffmpeg, args, { encoding: 'utf8' });

    if (result.error) {
        throw new Error(
            `getMeanVolumeDb: failed to run ffmpeg ('${ffmpeg}'). ` +
            `Install ffmpeg or set FFMPEG_PATH. Cause: ${result.error.message}`);
    }

    const log = `${result.stderr || ''}${result.stdout || ''}`;
    const match = /mean_volume:\s*(-inf|-?\d+(?:\.\d+)?) dB/.exec(log);

    if (!match) {
        throw new Error(
            `getMeanVolumeDb: could not parse mean_volume for ${mediaFile} ` +
            `(no audio stream, or ffmpeg failed). ffmpeg output:\n${log}`);
    }

    return match[1] === '-inf' ? -Infinity : parseFloat(match[1]);
}
