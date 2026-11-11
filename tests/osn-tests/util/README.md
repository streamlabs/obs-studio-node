# Shared OSN test utilities

Use this catalog to find reusable test helpers. Keep individual scenarios,
expected results, and suite commands with the tests that use them.

## Runtime and outputs

| Module | What it provides |
| --- | --- |
| [obs_handler.ts](obs_handler.ts) | `OBSHandler` manages the OSN runtime and optional default canvas, with helpers for OBS settings and legacy output signals. |
| [streaming_output.ts](streaming_output.ts) | `createStreamingOutput` creates a Simple or Advanced output with software x264 and, in Simple mode, AAC. It owns the output, encoders it creates, and signal callback. The caller owns the service, canvas, and any Advanced audio encoders it supplies. |
| [rtmp-test-server.ts](rtmp-test-server.ts) | `RtmpTestServer` owns a loopback TCP listener and bounded per-connection media captures. `RtmpTestSession` exposes packet snapshots, waits for encoded media, and can disconnect its publisher. |

Use the output fixture's `start()`, `stop()`, and `destroy()` methods and leave
its signal callback attached. Start waits for Starting, Activate, and Start;
stop waits for Stop and inactive capture in either order. Each attempt has its
own signals and a configurable deadline (10 seconds by default). Destroy the
output before releasing borrowed resources or shutting down OSN. A stop timeout
leaves the output and its encoders intact so active capture is not freed early.

Each RTMP connection has a distinct `attempt`. Use `waitForPublish` with
`afterAttempt` when reusing a stream key. `waitForMedia` counts encoded packets;
sequence headers do not count. Close the receiver to release connections and
settle pending waits. The receiver runs independently of OSN and does not require
a provider account or an external ingest endpoint.

## Media and protocol

| Module | What it provides |
| --- | --- |
| [rtmp-protocol.ts](rtmp-protocol.ts) | RTMP chunk decoding, message encoding, AMF0 encoding and decoding, and media packet parsing. |
| [rtmp-assertions.ts](rtmp-assertions.ts) | `expectAudioTracks` checks the exact track set and requires headers and frames for each track. `expectVideoFrames` requires encoded frames. `getAacAudioTrack` returns one AAC-LC track with ADTS framing. These helpers inspect captured packets without waiting for more media. |
| [media_probe.ts](media_probe.ts) | Reads audio stream titles and bitrates, video keyframes, and mean audio volume. `getVideoFrameRgb` decodes one unscaled RGB24 frame and validates its dimensions and payload length. |
| [media_assertions.ts](media_assertions.ts) | `expectRegionColor` checks the mean RGB color in a region of a decoded frame. It defaults to an 8×8 region and a tolerance of 25 per channel, and validates frame size and region bounds. |

Media probes look for `FFMPEG_PATH` or `FFPROBE_PATH`, then the packaged executable
(including macOS `Frameworks`), then `PATH`. The ffprobe lookup also checks the
local libobs build before falling back to `PATH`.

## Settings and common helpers

| Module | What it provides |
| --- | --- |
| [obs_enums.ts](obs_enums.ts) | Shared OBS output types and signals, input/filter/transition type names, and settings categories. |
| [input_settings.ts](input_settings.ts) | Sample settings for OBS inputs. |
| [filter_settings.ts](filter_settings.ts) | Sample settings for OBS filters. |
| [transition_settings.ts](transition_settings.ts) | Sample settings for OBS transitions. |
| [general.ts](general.ts) | Shared source and hotkey lists, timing helpers, file waits, test config cleanup, and error formatting. |

## Environment and reporting

| Module | What it provides |
| --- | --- |
| [env.ts](env.ts) | `loadTestEnv` loads the first available `.env` from the working directory or repository root, once per process, without overriding existing variables. |
| [user_pool_handler.ts](user_pool_handler.ts) | `UserPoolHandler` reserves and releases Twitch test accounts, returns their stream keys, and can exclude unhealthy accounts. |
| [cache-uploader.ts](cache-uploader.ts) | `CacheUploader` archives test cache files and uploads them to the test S3 bucket using configured upload credentials. |
| [error_messages.ts](error_messages.ts) | Common test error messages and placeholder formatting. |
| [logger.ts](logger.ts) | Test logging helpers for informational and warning messages. |
| [retry_context.ts](retry_context.ts) | Stores and clears Mocha retry errors by test identity so retry preparation can inspect the previous failure. |
| [list-reporter.js](list-reporter.js) | Mocha reporter that prints test outcomes and retry information and writes flaky test summaries for CI. |
