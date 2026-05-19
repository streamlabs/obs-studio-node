import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';

const testName = 'osn-video';

describe(testName, function() {
    this.timeout(80000); // REMOVEME DONT CHECKIN
    let obs: OBSHandler | null = null;
    let hasTestFailed: boolean = false;

    // Initialize OBS process
    before(function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName, false);
    });

    // Shutdown OBS process
    after(async function() {
        obs?.shutdown();

        if (hasTestFailed) {
            logInfo(testName, 'One or more test cases failed. Uploading cache');
            await obs?.uploadTestCache();
        }

        obs = null;
        deleteConfigFiles();
        logInfo(testName, 'Finished ' + testName + ' tests');
        logEmptyLine();
    });

    it('Attempt to start virtual camera and verify an exception is thrown', async () => {
        try {
            osn.NodeObs.OBS_service_startVirtualCam();
            expect.fail('Expected an error to be thrown when starting virtual camera without installing it first.');
        } catch (error: unknown) {
            expect(error).to.be.instanceOf(Error);
        }
    });
});