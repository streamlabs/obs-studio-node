import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles, sleep } from '../util/general';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { EFPSType } from '../osn';

const testName = 'osn-video';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    // Initialize OBS process
    before(function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName, false);
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

    it('Get skipped frames value', () => {
        const context = osn.VideoFactory.create();
        const videoInfo: osn.IVideoInfo = {
            fpsNum: 60,
            fpsDen: 1,
            baseWidth: 1280,
            baseHeight: 720,
            outputWidth: 1280,
            outputHeight: 720,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Bilinear,
            fpsType: osn.EFPSType.Fractional
        };
        context.video = videoInfo;

        // Getting skipped frames
        const skippedFrames = context.skippedFrames;

        // Checking if skipped frames was returned properly
        expect(skippedFrames).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.VideoSkippedFrames));
        expect(skippedFrames).to.equal(0, GetErrorMessage(ETestErrorMsg.VideoSkippedFramesWrongValue));
        context.destroy();
    });

    it('Get total frames value', () => {
        const context = osn.VideoFactory.create();
        const videoInfo: osn.IVideoInfo = {
            fpsNum: 60,
            fpsDen: 1,
            baseWidth: 1280,
            baseHeight: 720,
            outputWidth: 1280,
            outputHeight: 720,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Bilinear,
            fpsType: osn.EFPSType.Fractional
        };
        context.video = videoInfo;

        // Getting total frames value
        const totalFrames = context.encodedFrames;

        // Checking if total frames was returned properly
        expect(totalFrames).to.not.equal(undefined,  GetErrorMessage(ETestErrorMsg.VideoTotalFrames));
        expect(totalFrames).to.equal(0,  GetErrorMessage(ETestErrorMsg.VideoTotalFramesWrongValue));
        context.destroy();
    });

    it('Create and set video context', () => {
        const context = osn.VideoFactory.create();

        const newVideoContext: osn.IVideoInfo = {
            fpsNum: 120,
            fpsDen: 2,
            baseWidth: 3840,
            baseHeight: 2160,
            outputWidth: 1920,
            outputHeight: 1080,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: EFPSType.Fractional
        };
        context.video = newVideoContext;

        const currentVideo = context.video;

        expect(currentVideo.fpsNum).to.equal(120, GetErrorMessage(ETestErrorMsg.VideoSetFPSNum));
        expect(currentVideo.fpsDen).to.equal(2, GetErrorMessage(ETestErrorMsg.VideoSetFPSDen));
        expect(currentVideo.baseWidth).to.equal(3840, GetErrorMessage(ETestErrorMsg.VideoSetBaseWidth));
        expect(currentVideo.baseHeight).to.equal(2160, GetErrorMessage(ETestErrorMsg.VideoSetBaseHeight));
        expect(currentVideo.outputWidth).to.equal(1920, GetErrorMessage(ETestErrorMsg.VideoSetOutputWidth));
        expect(currentVideo.outputHeight).to.equal(1080, GetErrorMessage(ETestErrorMsg.VideoSetOutputHeight));
        // Note: OSN overrides ColorFormat, Colorspace, & Range with values from basic.ini. Not possible to change those atm from JS
        // So if this test fails then that means the default value was changed by a previous test or something. See osn::Video::SetVideoContext
        expect(currentVideo.outputFormat).to.equal(osn.EVideoFormat.NV12, GetErrorMessage(ETestErrorMsg.VideoSetOutputFormat));
        expect(currentVideo.colorspace).to.equal(osn.EColorSpace.CS709, GetErrorMessage(ETestErrorMsg.VideoSetColorFormat));
        expect(currentVideo.range).to.equal(osn.ERangeType.Partial, GetErrorMessage(ETestErrorMsg.VideoSetRange));
        expect(currentVideo.scaleType).to.equal(osn.EScaleType.Lanczos, GetErrorMessage(ETestErrorMsg.VideoSetScaleType));
        context.destroy();
    });

    it('Create and set second video context', () => {
        const context = osn.VideoFactory.create();

        const firstVideoInfo: osn.IVideoInfo = {
            fpsNum: 120,
            fpsDen: 2,
            baseWidth: 3840,
            baseHeight: 2160,
            outputWidth: 1920,
            outputHeight: 1080,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: EFPSType.Fractional
        };
        context.video = firstVideoInfo;

        const firstVideo = context.video;
        expect(firstVideo.fpsNum).to.equal(120, GetErrorMessage(ETestErrorMsg.VideoSetFPSNum));
        expect(firstVideo.fpsDen).to.equal(2, GetErrorMessage(ETestErrorMsg.VideoSetFPSDen));
        expect(firstVideo.baseWidth).to.equal(3840, GetErrorMessage(ETestErrorMsg.VideoSetBaseWidth));
        expect(firstVideo.baseHeight).to.equal(2160, GetErrorMessage(ETestErrorMsg.VideoSetBaseHeight));
        expect(firstVideo.outputWidth).to.equal(1920, GetErrorMessage(ETestErrorMsg.VideoSetOutputWidth));
        expect(firstVideo.outputHeight).to.equal(1080, GetErrorMessage(ETestErrorMsg.VideoSetOutputHeight));
        // Note: OSN overrides ColorFormat, Colorspace, & Range with values from basic.ini. Not possible to change those atm from JS
        // So if this test fails then that means the default value was changed by a previous test or something. See osn::Video::SetVideoContext
        expect(firstVideo.outputFormat).to.equal(osn.EVideoFormat.NV12, GetErrorMessage(ETestErrorMsg.VideoSetOutputFormat));
        expect(firstVideo.colorspace).to.equal(osn.EColorSpace.CS709, GetErrorMessage(ETestErrorMsg.VideoSetColorFormat));
        expect(firstVideo.range).to.equal(osn.ERangeType.Partial, GetErrorMessage(ETestErrorMsg.VideoSetRange));
        expect(firstVideo.scaleType).to.equal(osn.EScaleType.Lanczos, GetErrorMessage(ETestErrorMsg.VideoSetScaleType));

        const secondContext = osn.VideoFactory.create();

        const secondVideoInfo: osn.IVideoInfo = {
            fpsNum: 60,
            fpsDen: 2,
            baseWidth: 1080,
            baseHeight: 1920,
            outputWidth: 1080,
            outputHeight: 1920,
            outputFormat: osn.EVideoFormat.NV12,
            colorspace: osn.EColorSpace.CS709,
            range: osn.ERangeType.Partial,
            scaleType: osn.EScaleType.Lanczos,
            fpsType: EFPSType.Fractional
        };
        secondContext.video = secondVideoInfo;

        const secondVideo = secondContext.video;
        expect(secondVideo.fpsNum).to.equal(120, GetErrorMessage(ETestErrorMsg.VideoSetFPSNum));
        expect(secondVideo.fpsDen).to.equal(2, GetErrorMessage(ETestErrorMsg.VideoSetFPSDen));
        expect(secondVideo.baseWidth).to.equal(1080, GetErrorMessage(ETestErrorMsg.VideoSetBaseWidth));
        expect(secondVideo.baseHeight).to.equal(1920, GetErrorMessage(ETestErrorMsg.VideoSetBaseHeight));
        expect(secondVideo.outputWidth).to.equal(1080, GetErrorMessage(ETestErrorMsg.VideoSetOutputWidth));
        expect(secondVideo.outputHeight).to.equal(1920, GetErrorMessage(ETestErrorMsg.VideoSetOutputHeight));
        expect(secondVideo.outputFormat).to.equal(osn.EVideoFormat.NV12, GetErrorMessage(ETestErrorMsg.VideoSetOutputFormat));
        expect(secondVideo.colorspace).to.equal(osn.EColorSpace.CS709, GetErrorMessage(ETestErrorMsg.VideoSetColorFormat));
        expect(secondVideo.range).to.equal(osn.ERangeType.Partial, GetErrorMessage(ETestErrorMsg.VideoSetRange));
        expect(secondVideo.scaleType).to.equal(osn.EScaleType.Lanczos, GetErrorMessage(ETestErrorMsg.VideoSetScaleType));

        secondContext.destroy();

        context.destroy();
    });
});
