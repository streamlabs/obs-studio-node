import 'mocha';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
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
        // Note: OSN overrides ColorFormat, Colorspace, & Range with values from basic.ini. Not possible to change those atm from JS. See osn::Video::SetVideoContext
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
        // Note: OSN overrides ColorFormat, Colorspace, & Range with values from basic.ini. Not possible to change those atm from JS. See osn::Video::SetVideoContext
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

    it('Get video capture devices', function() {
        const devices = osn.NodeObs.OBS_settings_getVideoDevices();
        expect(devices).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.VideoDevices));
        expect(Array.isArray(devices)).to.equal(true, GetErrorMessage(ETestErrorMsg.VideoDevicesIsArray));
        for (const device of devices) {
            expect(device).to.have.property('id');
            expect(device).to.have.property('description');
            logInfo(testName, `Video Capture Device Found: ${device.description} with id: ${device.id}`);
        }
    });

    describe('Take screenshot', () => {
        const screenshotFormat = '%CCYY-%MM-%DD %hh-%mm-%ss';
        let context: osn.IVideo;
        let dir: string;

        // Reads the IHDR chunk of a PNG: 8-byte signature, 4-byte length, 4-byte "IHDR", then width and height.
        function readPngSize(file: string): { width: number; height: number } {
            const bytes = fs.readFileSync(file);
            expect(bytes.length).to.be.greaterThan(24, 'PNG is too short to hold a header');
            expect(bytes.subarray(0, 8)).to.deep.equal(Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), 'PNG signature mismatch');
            expect(bytes.subarray(12, 16).toString('ascii')).to.equal('IHDR', 'first PNG chunk is not IHDR');
            return { width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
        }

        beforeEach(() => {
            dir = fs.mkdtempSync(path.join(os.tmpdir(), 'osn-screenshot-'));
            context = osn.VideoFactory.create();
            context.video = {
                fpsNum: 30,
                fpsDen: 1,
                baseWidth: 1280,
                baseHeight: 720,
                outputWidth: 1280,
                outputHeight: 720,
                outputFormat: osn.EVideoFormat.NV12,
                colorspace: osn.EColorSpace.CS709,
                range: osn.ERangeType.Partial,
                scaleType: osn.EScaleType.Bilinear,
                fpsType: osn.EFPSType.Fractional,
            };
        });

        afterEach(() => {
            context.destroy();
            fs.rmSync(dir, { recursive: true, force: true });
        });

        it('Writes a PNG of the canvas at base resolution into the directory', async () => {
            const result = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, screenshotFormat, false);

            expect(result).to.not.equal(undefined, 'takeScreenshot returned nothing');
            expect(path.dirname(result.path)).to.equal(dir, 'screenshot was written outside the requested directory');
            expect(path.basename(result.path)).to.match(/^Screenshot \d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2}\.png$/);
            expect(result.width).to.equal(1280);
            expect(result.height).to.equal(720);

            const size = readPngSize(result.path);
            expect(size.width).to.equal(1280, 'PNG width does not match the canvas base width');
            expect(size.height).to.equal(720, 'PNG height does not match the canvas base height');
        });

        it('Never overwrites: a second screenshot with the same name gets a " (2)" suffix', async () => {
            // Same second, so the same generated name; the second call must wait for the
            // first file to land or the dedupe check will not see it yet.
            const first = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, 'same-name', false);
            const second = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, 'same-name', false);

            expect(path.basename(first.path)).to.equal('Screenshot same-name.png');
            expect(path.basename(second.path)).to.equal('Screenshot same-name (2).png');
            expect(fs.existsSync(first.path)).to.equal(true);
            expect(fs.existsSync(second.path)).to.equal(true);
        });

        it('Replaces spaces with underscores when noSpace is set', async () => {
            const result = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, 'no space', true);
            expect(path.basename(result.path)).to.equal('Screenshot_no_space.png');

            const again = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, 'no space', true);
            expect(path.basename(again.path)).to.equal('Screenshot_no_space_2.png');
        });

        it('Rejects when the directory does not exist', async () => {
            const missing = path.join(dir, 'does-not-exist');
            let error: Error;
            try {
                await osn.NodeObs.OBS_content_takeScreenshot(context, missing, screenshotFormat, false);
            } catch (e) {
                error = e;
            }
            expect(error).to.not.equal(undefined, 'expected the promise to reject');
        });

        it('Throws when the first argument is not a video context', () => {
            // N-API rejects the unwrap of a plain object ("Invalid argument") before the binding's own check runs.
            expect(() => osn.NodeObs.OBS_content_takeScreenshot({} as any, dir, screenshotFormat, false)).to.throw(
                /Invalid argument|not a Video object/,
            );
        });

        it('Throws synchronously when given an empty array of canvases', () => {
            expect(() => osn.NodeObs.OBS_content_takeScreenshot([], dir, screenshotFormat, false)).to.throw(/must not be empty/);
        });

        it('Returns a pending promise immediately instead of blocking the caller', async () => {
            const callStart = Date.now();
            const promise = osn.NodeObs.OBS_content_takeScreenshot(context, dir, screenshotFormat, false);
            const callDuration = Date.now() - callStart;

            expect(promise).to.be.instanceOf(Promise);
            expect(callDuration).to.be.lessThan(500, 'takeScreenshot blocked the caller instead of returning a pending promise');

            const result = await promise;
            expect(fs.existsSync(result.path)).to.equal(true);
        });

        it('Creates a missing subfolder named by the filename format', async () => {
            const result = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, 'subdir/%CCYY-%MM-%DD %hh-%mm-%ss', false);

            expect(fs.existsSync(result.path)).to.equal(true);
            expect(path.dirname(result.path)).to.not.equal(dir, 'expected a subfolder to have been created');
            expect(fs.existsSync(path.dirname(result.path))).to.equal(true);
        });

        it('Captures every canvas from one call and suffixes each name with its resolution', async () => {
            const secondContext = osn.VideoFactory.create();
            secondContext.video = {
                fpsNum: 30,
                fpsDen: 1,
                baseWidth: 640,
                baseHeight: 480,
                outputWidth: 640,
                outputHeight: 480,
                outputFormat: osn.EVideoFormat.NV12,
                colorspace: osn.EColorSpace.CS709,
                range: osn.ERangeType.Partial,
                scaleType: osn.EScaleType.Bilinear,
                fpsType: osn.EFPSType.Fractional,
            };

            try {
                const results = await osn.NodeObs.OBS_content_takeScreenshot([context, secondContext], dir, screenshotFormat, false);

                expect(results).to.have.lengthOf(2);
                expect(path.basename(results[0].path)).to.match(/^Screenshot \d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2} 1280x720\.png$/);
                expect(path.basename(results[1].path)).to.match(/^Screenshot \d{4}-\d{2}-\d{2} \d{2}-\d{2}-\d{2} 640x480\.png$/);

                const firstSize = readPngSize(results[0].path);
                expect(firstSize.width).to.equal(1280);
                expect(firstSize.height).to.equal(720);

                const secondSize = readPngSize(results[1].path);
                expect(secondSize.width).to.equal(640);
                expect(secondSize.height).to.equal(480);
            } finally {
                secondContext.destroy();
            }
        });

        it('Rejects a batch larger than the 4-job limit without writing any file', async () => {
            let error: Error;
            try {
                await osn.NodeObs.OBS_content_takeScreenshot([context, context, context, context, context], dir, screenshotFormat, false);
            } catch (e) {
                error = e;
            }
            expect(error).to.not.equal(undefined, 'expected the promise to reject');
            expect(error.message).to.match(/busy/);
            expect(fs.readdirSync(dir)).to.have.lengthOf(0, 'a rejected batch must not write any screenshot');

            const result = await osn.NodeObs.OBS_content_takeScreenshot(context, dir, screenshotFormat, false);
            expect(fs.existsSync(result.path)).to.equal(true);
            expect(fs.readdirSync(dir)).to.have.lengthOf(1);
        });
    });
});
