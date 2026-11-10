# Shared streaming test utilities

Use these utilities before adding setup, lifecycle or media-assertion helpers to a
new suite. Keep provider-specific settings, deliberate invalid configurations and
expected results in the test.

| Utility | Responsibility and ownership |
| --- | --- |
| `OBSHandler` | Initializes and shuts down the OSN runtime. |
| `createStreamingOutput` (`streaming_output.ts`) | Owns one standard Simple/Advanced output, its software encoders and its signal callback. Borrows the supplied service and canvas. |
| `RtmpTestServer` (`rtmp-test-server.ts`) | Owns the loopback listener, connections and per-attempt media captures. Independent of OSN. |
| `expectAudioTracks`, `expectVideoFrames` (`rtmp-assertions.ts`) | Assert received media. They neither start outputs nor wait for packets. |
| `media_probe.ts` | Uses ffprobe/ffmpeg for recorded-file inspection. |
| `expectRegionColor` (`media_assertions.ts`) | Asserts the mean RGB color of a region in a decoded frame. No runtime or output ownership. |

## Selective recording

`src/test_osn_selective_recording.ts` records generated color sources without a
provider account, streaming output or replay buffer. It checks decoded RGB frames
for stream-only, recording-only, both-visible and neither-visible scene items.
An always-visible background also rejects empty or black recordings. Both Simple
quality presets, disabled selective recording and an Advanced dedicated encoder
are covered. The suite is discovered by the existing integration/CI scripts.

`getVideoFrameRgb()` in `media_probe.ts` decodes one unscaled frame and rejects
missing frames or unexpected dimensions. FFmpeg emits binary PPM so the decoded
width and height are checked independently, followed by the RGB payload length.
`src/test_media_probe.ts` covers equal-pixel-count dimension mismatches, exact RGB
bytes and missing frames without starting OBS. FFmpeg lookup honors `FFMPEG_PATH`, then
checks the OSN package root and macOS `Frameworks` directory, then PATH. Color
checks sample inside the blocks and tolerate compression differences.

`expectRegionColor()` accepts `{ data, width, height }` for an RGB24 frame and a
region `{ x, y, width?, height? }` measured from its top-left pixel. Region size
defaults to 8x8 and the per-channel tolerance defaults to 25; both are configurable.
It rejects invalid frame dimensions, buffer lengths, region bounds and tolerances.
Keep expected colors and the choice of interior regions in each test.

After building and installing OSN, run both suites with:

```text
yarn electron-mocha -t 30000 -r ts-node/register tests/osn-tests/src/test_osn_selective_recording.ts tests/osn-tests/src/test_media_probe.ts
```

## Output setup and lifetime

`createStreamingOutput({ mode, name, video, service, timeoutMs? })` configures
x264 at 500 Kbps with a two-second keyframe interval and the ultrafast preset.
Simple mode gets a 160 Kbps AAC encoder. Advanced mode uses caller-registered
audio tracks (native defaults: main track 1, VOD track 2). Service bitrate
enforcement, delay and reconnect are disabled. VOD keeps its native disabled
default; enable it explicitly when the scenario requires it.

Simple VOD reads source audio track 2 and preserves source mixer flags. Advanced
VOD reads the caller-selected `twitchTrack`. `getAacAudioTrack()` in
`rtmp-assertions.ts` returns received AAC-LC with ADTS framing; write it to a
test-owned file and use `getMeanVolumeDb()` to check audible audio and exclusions.

Configure the exposed `stream` while stopped, including replacing its borrowed
service. Use the fixture's `start()`, `stop()` and `destroy()` methods and retain
its signal callback. `start()` waits for Starting, Activate and Start;
`stop()` force-stops and waits for Stop and inactive capture in either signal
order. Signals belong to one output and one explicit start attempt. This permits
simultaneous outputs and retained starts without consuming another output's
events from `OBSHandler`'s legacy shared signal queue.

Waits default to ten seconds. Failures include the output name and native error
or signal history. Stop and destroy are safe to repeat. A terminal native error
is reported after stop has settled; destroy releases the output and its encoders
even in that case. A stop timeout leaves ownership intact rather than releasing
an output whose capture may still be active. Destroy outputs before releasing
their borrowed services/canvases or calling `OBSHandler.shutdown()`.

## Receiving and asserting media

`RtmpTestServer` in `rtmp-test-server.ts` receives real encoded output on an
automatically assigned `127.0.0.1` port. It uses Node's TCP support and does not
require a provider account, stream key, external endpoint, or extra server process.
Use the existing `OBSHandler` to initialize OSN separately.

The suite owns the service, canvas and fixture lifetime. With an initialized
`obs: OBSHandler`, a complete Simple output test can use:

```ts
const receiver = await RtmpTestServer.start();
let service: osn.IService;
let output: ITestStreamingOutput;
try {
    service = osn.ServiceFactory.create('rtmp_custom', 'example', {
        server: receiver.url, key: 'example',
    });
    output = createStreamingOutput({
        mode: 'Simple', name: 'example', video: obs.defaultVideoContext, service,
    });
    await output.start();
    const publication = await receiver.waitForPublish('example');
    await publication.waitForMedia({ audioPackets: 25, videoPackets: 15 });
    await output.stop();
    expectAudioTracks(publication, [0]);
    expectVideoFrames(publication);
    receiver.assertHealthy();
} finally {
    try {
        if (output) await output.destroy();
        if (service) osn.ServiceFactory.destroy(service);
    } finally {
        await receiver.close();
    }
}
```

Each TCP connection has a distinct `attempt`. Use
`waitForPublish(key, { afterAttempt: previous.attempt })` after restarting the same
output or calling `publication.disconnect()` to trigger a reconnect. Captures are
separate for simultaneous publishers and subsequent attempts with the same key.
`waitForMedia` counts encoded frames; sequence headers alone cannot satisfy it.
Snapshots expose media kind, codec FourCC, track ID, packet type, timestamp, and
size. `expectAudioTracks` requires the exact track set, plus a sequence header and
encoded frames for each expected track. Expected track order is irrelevant.
`expectVideoFrames` requires encoded video; sequence headers and end-of-sequence
messages alone fail. Choose capture lengths in the test and stop the publisher
before asserting the absence of unwanted tracks.

The receiver supports the RTMP handshake, publisher commands, chunk compression,
extended timestamps, control messages, AMF0 metadata, legacy H.264/AAC, and enhanced
one-track-per-message headers. Unsupported media formats and protocol errors fail
explicitly. Captures and message sizes are bounded; waits have deadlines, and
closing the receiver settles pending waits. This fixture does not model provider
API behavior or remote ingest acceptance.

`src/test_rtmp_test_server.ts` exercises protocol parsing, receiver lifetime and
media assertions without OBS. `src/test_osn_streaming_vod.ts` and the local
`Start streaming` case in `src/test_osn_simple_streaming.ts` share the output
fixture and assertions with the real OSN client/server. All are discovered by
`test:integration`. For a focused run after building and installing OSN, this
filter excludes the Simple suite's remaining provider-account tests:

```text
yarn electron-mocha -t 30000 -r ts-node/register tests/osn-tests/src/test_rtmp_test_server.ts tests/osn-tests/src/test_osn_streaming_vod.ts tests/osn-tests/src/test_osn_simple_streaming.ts --grep "RTMP test receiver|osn-streaming-vod|osn-simple-streaming \(local\)"
```

`src/test_nodeobs_simple_vod_settings.ts` separately verifies Simple VOD settings
visibility, persistence, and independence from Advanced track selections without
using a provider account.

The native `test-nodeobs-enhanced-broadcasting-vod.cpp` tests call the older
NodeObs start handler with a local Enhanced Broadcasting configuration file and
inspect its live/VOD audio encoders. They cover conflicting Simple/Advanced VOD
preferences, Simple's fixed track 2 and advanced encoder settings requirement,
and Advanced's selected track 6. The ingest endpoint is loopback; these tests do
not contact Twitch or check media delivery. After building OSN, run them with:

```text
ctest --test-dir build -C RelWithDebInfo -R "NodeObs.*Enhanced Broadcasting" --output-on-failure --no-tests=error
```

## Canvas audio routing

`src/test_osn_canvas_audio_routing.ts` keeps two loopback streams active while
changing shared-source visibility. Simple and Advanced outputs run with selective
rendering enabled and disabled. Decoded AAC windows check independent canvas
silence, visibility restoration, source mixer changes, global audio roots,
duplicate-path gain, nested visibility and canvas restrictions, and unassigned
scene-item canvases. Monitoring stays disabled throughout.

Each measurement waits for encoded packet timestamps to pass a one-second
settling interval, then decodes a bounded half-second window with its original
AAC configuration. An audible generated-tone baseline rejects broken capture
setup, and duplicate paths must retain that baseline's gain. Output fixtures own
their encoders; scene items are removed before their canvases are destroyed.
The suite logs its unique directory under `osnData/osn-canvas-audio-routing-*`
and retains the input, AAC windows, decoded WAV windows and `measurements.json` there.

After building and installing OSN, run the focused suite with:

```text
yarn electron-mocha -t 60000 -r ts-node/register tests/osn-tests/src/test_osn_canvas_audio_routing.ts
```
