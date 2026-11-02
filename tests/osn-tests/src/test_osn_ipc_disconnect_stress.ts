import 'mocha'
import { expect } from 'chai'
import * as fs from 'fs';
import * as path from 'path';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';

const testName = 'osn-ipc-disconnect-stress';

// Stress coverage for Controller::m_connection locking:
// poller threads (the global source callback worker and each output's
// WorkerSignals worker) copy the IPC connection through
// Controller::GetConnection() every ~33 ms, while IPC.disconnect() on the JS
// thread drops it. Before the connection was guarded by a mutex, a poller
// could copy a shared_ptr that was being freed and crash in
// ipc::client_win::call. A crash here takes down the whole test process.
describe(testName, () => {
    const cycles = 8;
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    before(function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);
    });

    after(async function() {
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

    // Pollers started by the current test. obs.shutdown() does not stop them,
    // so afterEach stops whatever a failed test left running.
    const activeRecordings: osn.IAdvancedRecording[] = [];
    let globalPollerActive = false;

    afterEach(function() {
        stopAllPollers();

        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    // Starts and stops a recording so its WorkerSignals thread is running.
    // The worker keeps polling until stopWorker() runs, so it is still active
    // when the caller disconnects. Returns null on macOS, where the other
    // recording worker tests are skipped.
    async function startRecordingPoller(tag: string): Promise<osn.IAdvancedRecording | null> {
        if (obs.isDarwin()) {
            return null;
        }

        const recording = osn.AdvancedRecordingFactory.create();
        // Track it before any step below can throw, so afterEach can stop its worker.
        activeRecordings.push(recording);
        recording.path = path.join(path.normalize(__dirname), '..', 'osnData');
        recording.format = osn.ERecordingFormat.MP4;
        recording.useStreamEncoders = false;
        recording.videoEncoder = osn.VideoEncoderFactory.create('obs_x264', 'video-encoder-' + tag);
        recording.overwrite = false;
        recording.noSpace = false;
        recording.video = obs.defaultVideoContext;
        const track = osn.AudioTrackFactory.create(160, 'track-' + tag);
        osn.AudioTrackFactory.setAtIndex(track, 1);
        recording.signalHandler = (signal) => { obs.signals.push(signal); };

        recording.start();

        const startInfo = await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Start);
        if (startInfo.signal === EOBSOutputSignal.Stop) {
            throw Error('Recording did not start: code=' + startInfo.code + ' error=' + startInfo.error);
        }

        await sleep(300);

        recording.stop();
        await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Stopping);
        await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Stop);
        await obs.getNextSignalInfo(EOBSOutputType.Recording, EOBSOutputSignal.Wrote);

        const file = recording.lastFile();
        if (file && fs.existsSync(file)) {
            fs.unlinkSync(file);
        }

        return recording;
    }

    // Stops a recording's worker without IPC. Setting signalHandler calls
    // stopWorker() and joins the thread; Factory.destroy() would need a live
    // connection, and the wrapper's uid means nothing to a new server.
    // Safe whether or not the worker was ever started.
    function stopRecordingPoller(recording: osn.IAdvancedRecording | null) {
        if (!recording) {
            return;
        }

        const index = activeRecordings.indexOf(recording);
        if (index >= 0) {
            activeRecordings.splice(index, 1);
        }
        recording.signalHandler = () => {};
    }

    function startGlobalPoller() {
        osn.NodeObs.RegisterSourceCallback(() => {});
        globalPollerActive = true;
    }

    // The global worker returns on its own once the connection is gone;
    // this joins it and releases its thread-safe function.
    function stopGlobalPoller() {
        if (!globalPollerActive) {
            return;
        }

        globalPollerActive = false;
        osn.NodeObs.RemoveSourceCallback();
    }

    // Neither stop makes an IPC call, so this works connected or disconnected.
    function stopAllPollers() {
        while (activeRecordings.length > 0) {
            stopRecordingPoller(activeRecordings[activeRecordings.length - 1]);
        }
        stopGlobalPoller();
    }

    // The server is gone, so the old video context must not be destroyed over IPC.
    function disconnectWithActivePollers() {
        osn.NodeObs.IPC.disconnect();
        obs.defaultVideoContext = null;
    }

    function expectResponsiveSession() {
        const types = osn.InputFactory.types();
        expect(types.length).to.be.greaterThan(0, 'New session did not answer an IPC call');

        const recording = osn.AdvancedRecordingFactory.create();
        expect(recording).to.not.equal(undefined, 'Could not create an output in the new session');
        osn.AdvancedRecordingFactory.destroy(recording);
    }

    it(`Disconnects while pollers are active, ${cycles} cycles`, async function() {
        this.timeout(cycles * 30000);

        for (let i = 0; i < cycles; i++) {
            startGlobalPoller();
            const recording = await startRecordingPoller('stress-' + i);

            // Vary where in the ~33 ms poll cycle the disconnect lands.
            await sleep((i * 17) % 100);

            logInfo(testName, `Cycle ${i + 1}/${cycles}: disconnecting with active pollers`);
            disconnectWithActivePollers();

            // Let the workers spin a few cycles with no connection.
            await sleep(100);

            stopRecordingPoller(recording);
            stopGlobalPoller();

            obs.reconnect();
        }

        expectResponsiveSession();
    });

    it('Keeps an output poller running across a reconnect', async function() {
        if (obs.isDarwin()) {
            this.skip();
        }
        this.timeout(60000);

        const recording = await startRecordingPoller('survive');

        disconnectWithActivePollers();
        obs.reconnect();

        // The worker now copies the new connection and keeps querying a uid
        // from the old session; the new server must answer it, not crash.
        await sleep(500);
        expectResponsiveSession();

        stopRecordingPoller(recording);
    });
});
