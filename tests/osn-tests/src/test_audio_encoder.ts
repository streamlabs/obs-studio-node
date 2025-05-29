import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles } from '../util/general';
import { EOBSInputTypes } from '../util/obs_enums';
import { encode } from 'punycode';

const testName = 'osn-audio-encoder';

describe(testName, () => {
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

    it('Create an audio encoder', () => {
        const encoder = osn.AudioEncoderFactory.create("ffmpeg_aac", "audio-encoder-test-1");
        expect(encoder).to.not.equal(undefined, 'Invalid audio encoder creation');
        expect(encoder.name).to.equal('audio-encoder-test-1', "Invalid default name value");
        expect(encoder.bitrate).to.equal(128, "Invalid default bitrate value");

        encoder.name = 'audio track';
        encoder.bitrate = 256;

        expect(encoder.name).to.equal('audio track', "Invalid name value");
        expect(encoder.bitrate).to.equal(256, "Invalid bitrate value");

        encoder.release();
    });
});
