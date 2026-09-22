# Local RTMP streaming tests

`RtmpTestServer` in `rtmp-test-server.ts` receives real encoded output on an
automatically assigned `127.0.0.1` port. It uses Node's TCP support and does not
require a provider account, stream key, external endpoint, or extra server process.
Use the existing `OBSHandler` to initialize OSN separately.

The consuming suite owns native output lifetime. In this sketch, `startOutput()`
waits for native Start and `stopOutput()` waits for terminal Stop and any active
capture to Deactivate. See `src/test_osn_streaming_vod.ts` for a complete example
that handles both signal orders and retained instances.

```ts
const receiver = await RtmpTestServer.start();
try {
    service.update({ server: receiver.url, key: 'vertical' });
    await startOutput();
    const publication = await receiver.waitForPublish('vertical');
    await publication.waitForMedia({ audioPackets: 25, videoPackets: 15 });
    await stopOutput();
    // Assert the encoded media contract here, using snapshot() and audioTrackIds().
    receiver.assertHealthy();
} finally {
    try {
        await stopOutput(); // The suite's stop helper is safe to call again.
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
size. Assertions belong to the consuming test rather than the receiver.

The receiver supports the RTMP handshake, publisher commands, chunk compression,
extended timestamps, control messages, AMF0 metadata, legacy H.264/AAC, and enhanced
one-track-per-message headers. Unsupported media formats and protocol errors fail
explicitly. Captures and message sizes are bounded; waits have deadlines, and
closing the receiver settles pending waits. This fixture does not model provider
API behavior or remote ingest acceptance.

`src/test_rtmp_test_server.ts` exercises protocol parsing and receiver lifetime
without OBS. `src/test_osn_streaming_vod.ts` uses the fixture with the real OSN
client/server and software H.264/AAC. Both are discovered by the existing
`test:integration` command. For a focused run after building and installing OSN:

```text
yarn electron-mocha -t 30000 -r ts-node/register tests/osn-tests/src/test_rtmp_test_server.ts tests/osn-tests/src/test_osn_streaming_vod.ts
```
