import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles } from '../util/general';
import { EOBSInputTypes } from '../util/obs_enums';

const testName = 'osn-video-encoder';

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

    it('Get encoder types', () => {
        const videoEncoders = osn.VideoEncoderFactory.types();
        expect(videoEncoders).to.contain('obs_x264', 'x264 video encoder not present');
    });

    it('Create a video encoder', () => {
        const encoder = osn.VideoEncoderFactory.create('obs_x264', "My x264", {});
        expect(encoder).to.not.equal(undefined, 'Invalid x264 video encoder creation');
        expect(encoder.active).to.equal(false, "Invalid active value");
        expect(encoder.lastError).to.equal('', "Error while creating the video encoder");

        const props = encoder.properties;
        let prop: any = props.first();
        const propsArray = [];
        while (prop) {
            if (prop)
                propsArray.push({
                    name: prop.name,
                    value: prop.value
                });
            prop = prop.next();
        }

        expect(propsArray[0].name).to.equal('rate_control', "Invalid rate_control name property");
        expect(propsArray[0].value).to.equal('CBR', "Invalid rate_control value property");

        expect(propsArray[1].name).to.equal('bitrate', "Invalid bitrate name property");
        expect(propsArray[1].value).to.equal(2500, "Invalid bitrate value property");

        expect(propsArray[2].name).to.equal('use_bufsize', "Invalid use_bufsize name property");
        expect(propsArray[2].value).to.equal(false, "Invalid use_bufsize value property");

        expect(propsArray[3].name).to.equal('buffer_size', "Invalid buffer_size name property");
        expect(propsArray[3].value).to.equal(2500, "Invalid buffer_size value property");

        expect(propsArray[4].name).to.equal('crf', "Invalid crf name property");
        expect(propsArray[4].value).to.equal(23, "Invalid crf value property");

        expect(propsArray[5].name).to.equal('keyint_sec', "Invalid keyint_sec name property");
        expect(propsArray[5].value).to.equal(0, "Invalid keyint_sec value property");

        expect(propsArray[6].name).to.equal('preset', "Invalid preset name property");
        expect(propsArray[6].value).to.equal('veryfast', "Invalid preset value property");

        expect(propsArray[7].name).to.equal('profile', "Invalid profile name property");
        expect(propsArray[7].value).to.equal('', "Invalid profile value property");

        expect(propsArray[8].name).to.equal('tune', "Invalid tune name property");
        expect(propsArray[8].value).to.equal('', "Invalid tune value property");

        expect(propsArray[9].name).to.equal('x264opts', "Invalid x264opts name property");
        expect(propsArray[9].value).to.equal('', "Invalid x264opts value property");

        expect(propsArray[10].name).to.equal('repeat_headers', "Invalid repeat_headers name property");
        expect(propsArray[10].value).to.equal(false, "Invalid repeat_headers value property");
    });

    it('Update video encoder properties', () => {
        const encoder = osn.VideoEncoderFactory.create('obs_x264', "My x264", {});
        encoder.update({
            rate_control: 'VBR',
            bitrate: 5000,
            use_bufsize: true,
            buffer_size: 6000,
            crf: 26,
            keyint_sec: 1,
            preset: 'fast',
            profile: 'main',
            tune: 'film',
            repeat_headers: true
        })

        const props = encoder.properties;
        let prop: any = props.first();
        const propsArray = [];
        while (prop) {
            if (prop)
                propsArray.push({
                    name: prop.name,
                    value: prop.value
                });
            prop = prop.next();
        }

        expect(propsArray[0].name).to.equal('rate_control', "Invalid rate_control name property");
        expect(propsArray[0].value).to.equal('VBR', "Invalid rate_control value property");

        expect(propsArray[1].name).to.equal('bitrate', "Invalid bitrate name property");
        expect(propsArray[1].value).to.equal(5000, "Invalid bitrate value property");

        expect(propsArray[2].name).to.equal('use_bufsize', "Invalid use_bufsize name property");
        expect(propsArray[2].value).to.equal(true, "Invalid use_bufsize value property");

        expect(propsArray[3].name).to.equal('buffer_size', "Invalid buffer_size name property");
        expect(propsArray[3].value).to.equal(6000, "Invalid buffer_size value property");

        expect(propsArray[4].name).to.equal('crf', "Invalid crf name property");
        expect(propsArray[4].value).to.equal(26, "Invalid crf value property");

        expect(propsArray[5].name).to.equal('keyint_sec', "Invalid keyint_sec name property");
        expect(propsArray[5].value).to.equal(1, "Invalid keyint_sec value property");

        expect(propsArray[6].name).to.equal('preset', "Invalid preset name property");
        expect(propsArray[6].value).to.equal('fast', "Invalid preset value property");

        expect(propsArray[7].name).to.equal('profile', "Invalid profile name property");
        expect(propsArray[7].value).to.equal('main', "Invalid profile value property");

        expect(propsArray[8].name).to.equal('tune', "Invalid tune name property");
        expect(propsArray[8].value).to.equal('film', "Invalid tune value property");

        expect(propsArray[9].name).to.equal('x264opts', "Invalid x264opts name property");
        expect(propsArray[9].value).to.equal('', "Invalid x264opts value property");

        expect(propsArray[10].name).to.equal('repeat_headers', "Invalid repeat_headers name property");
        expect(propsArray[10].value).to.equal(true, "Invalid repeat_headers value property");
    });
});
