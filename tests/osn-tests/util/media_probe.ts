import { spawnSync } from 'child_process';
import * as fs from 'fs';

// Resolves an ffmpeg executable. Honours FFMPEG_PATH (point it at OBS's bundled
// ffmpeg when ffmpeg is not on PATH), otherwise relies on `ffmpeg` from PATH.
function resolveFfmpeg(): string {
    const override = process.env.FFMPEG_PATH;
    if (override && fs.existsSync(override)) {
        return override;
    }
    return 'ffmpeg';
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
