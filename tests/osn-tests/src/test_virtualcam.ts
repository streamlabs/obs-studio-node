import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles, sleep } from '../util/general';
import { EOBSOutputSignal, EOBSOutputType } from '../util/obs_enums';

const testName = 'osn-virtualcam';

describe(testName, function() {
    this.timeout(100000);
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    // Initialize OBS process
    before(function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);
    });

    // Shutdown OBS process
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

    afterEach(function() {
        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    it('Test virtualcam output', async function() {
        if (!process.env.SLOBS_LOCAL_DEVELOPER || !obs.isDarwin()) {
            logInfo(testName, "Skipping this test it must be ran locally on a macOS Apple developer machine.")
            this.skip(); // This should only be ran locally because this test installs a SystemExtension which will not work on a VM
        }
        osn.NodeObs.requestVirtualCamInstallation((isInstalled) => {
            console.log(`isInstalled: ${isInstalled}`);
        });

        // Registers the global callback.
        // This step must be invoked before OBS_service_createVirtualCam
        obs.connectOutputSignals();

        // Special step on MacOS to create the output source.
        // Behind the scenes, this will install the SystemExtension
        osn.NodeObs.OBS_service_createVirtualCam();

        const signalInfo = await obs.getNextSignalInfo(
            EOBSOutputType.VirtualCam, EOBSOutputSignal.ReconnectSuccess);
        expect(signalInfo.type).to.equal(
            EOBSOutputType.VirtualCam, GetErrorMessage(ETestErrorMsg.VirtualCamStoppedWithError));

        // On MacOS, we should check this error string and display it to inform the user.
        let errorMsg = osn.NodeObs.OBS_service_startVirtualCam();
        expect(errorMsg === undefined);
        osn.NodeObs.OBS_service_stopVirtualCam();
    });
});
