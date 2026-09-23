import { EventEmitter } from 'events';
import * as osn from '../osn';
import { EOBSOutputSignal, EOBSOutputType } from './obs_enums';

export type TStreamingMode = 'Simple' | 'Advanced';

export interface IStreamingOutputOptions {
    mode: TStreamingMode;
    name: string;
    video: osn.IVideo;
    service: osn.IService;
    timeoutMs?: number;
}

export interface ITestStreamingOutput {
    readonly stream: osn.ISimpleStreaming | osn.IAdvancedStreaming;
    /** Starts a new attempt and waits for Starting, Activate and Start. Rejects native failures. */
    start(): Promise<void>;
    /** Force-stops and waits for Stop and inactive capture, in either signal order. Safe to repeat. */
    stop(): Promise<void>;
    /** Stops, then releases the output and its encoders. Safe to repeat. */
    destroy(): Promise<void>;
}

/**
 * Creates a standard streaming output with software H.264/AAC and no delay or reconnect.
 * Owns the output, its signal callback and its encoders; borrows the service and canvas.
 * Advanced mode uses caller-registered audio tracks (native defaults: main 1, VOD 2).
 * Configure the exposed stream while stopped, but use these start/stop methods and
 * leave signalHandler attached. VOD remains disabled unless the test enables it.
 *
 * Waits default to 10 seconds and include this output's signal history on timeout.
 * A native failure rejects start/stop; destroy still releases a terminal output.
 * A stop timeout leaves ownership intact so an active output is never freed early.
 * Destroy before releasing borrowed services/canvases or shutting down OBS.
 */
export function createStreamingOutput(options: IStreamingOutputOptions): ITestStreamingOutput {
    const { mode, name, video, service, timeoutMs = 10000 } = options;
    const stream = mode === 'Advanced' ? osn.AdvancedStreamingFactory.create() : osn.SimpleStreamingFactory.create();
    let encoder: osn.IVideoEncoder;
    let audio: osn.IAudioEncoder;
    let signals: osn.EOutputSignal[] = [];
    let started = false;
    let captureActive = false;
    let destroyed = false;
    const events = new EventEmitter();

    function release() {
        if (destroyed) return;
        stream.signalHandler = () => {};
        if (mode === 'Advanced') osn.AdvancedStreamingFactory.destroy(stream as osn.IAdvancedStreaming);
        else osn.SimpleStreamingFactory.destroy(stream as osn.ISimpleStreaming);
        destroyed = true;
        if (encoder) encoder.release();
        if (audio) audio.release();
    }

    try {
        encoder = osn.VideoEncoderFactory.create('obs_x264', name, {
            bitrate: 500, keyint_sec: 2, preset: 'ultrafast', rate_control: 'CBR',
        });
        stream.video = video;
        stream.videoEncoder = encoder;
        stream.service = service;
        stream.enforceServiceBitrate = false;
        stream.delay = osn.DelayFactory.create();
        stream.delay.enabled = false;
        stream.reconnect = osn.ReconnectFactory.create();
        stream.reconnect.enabled = false;
        stream.network = osn.NetworkFactory.create();
        if (mode === 'Simple') {
            audio = osn.AudioEncoderFactory.create('ffmpeg_aac', `${name}-audio`);
            audio.bitrate = 160;
            (stream as osn.ISimpleStreaming).audioEncoder = audio;
        }
        stream.signalHandler = signal => {
            signals.push(signal);
            if (signal.signal === EOBSOutputSignal.Activate) captureActive = true;
            if (signal.signal === EOBSOutputSignal.Deactivate) captureActive = false;
            events.emit('signal');
        };
    } catch (error) {
        release();
        throw error;
    }

    function hasSignal(name: EOBSOutputSignal): boolean {
        return signals.some(signal => signal.type === EOBSOutputType.Streaming && signal.signal === name);
    }

    function failure(): Error | undefined {
        const failed = signals.find(signal =>
            (signal.signal === EOBSOutputSignal.Stop && signal.code !== osn.EOutputCode.Success) ||
            signal.signal === EOBSOutputSignal.WriteError);
        return failed ? new Error(`Native ${name} failed: ${JSON.stringify(failed)}`) : undefined;
    }

    function waitForSignals(ready: () => boolean, description: string, rejectFailure = true): Promise<void> {
        return new Promise((resolve, reject) => {
            const finish = (error?: Error) => {
                clearTimeout(timer);
                events.removeListener('signal', check);
                if (error) reject(error); else resolve();
            };
            const check = () => {
                const error = rejectFailure ? failure() : undefined;
                if (error) finish(error);
                else if (ready()) finish();
            };
            const timer = setTimeout(() => finish(new Error(
                `Native ${name} timed out waiting for ${description}: ${JSON.stringify(signals)}`)), timeoutMs);
            events.on('signal', check);
            check();
        });
    }

    async function stop() {
        if (!started) return;
        if (!hasSignal(EOBSOutputSignal.Stop)) stream.stop(true);
        // A failed attempt still needs its terminal signals before resources can be released.
        await waitForSignals(() => hasSignal(EOBSOutputSignal.Stop) && !captureActive, 'Stop and inactive capture', false);
        started = false;
        const error = failure();
        if (error) throw error;
    }

    return {
        stream,
        async start() {
            if (destroyed) throw new Error(`Native ${name} has been destroyed`);
            if (started) throw new Error(`Native ${name} must be stopped before starting again`);
            signals = [];
            captureActive = false;
            started = true;
            try {
                stream.start();
            } catch (error) {
                started = false;
                throw error;
            }
            await waitForSignals(() => [EOBSOutputSignal.Starting, EOBSOutputSignal.Activate, EOBSOutputSignal.Start]
                .every(hasSignal), 'Starting, Activate and Start');
        },
        stop,
        async destroy() {
            try { await stop(); }
            finally { if (!started) release(); }
        },
    };
}
