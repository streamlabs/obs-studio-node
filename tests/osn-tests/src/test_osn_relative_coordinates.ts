import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import { logEmptyLine, logInfo } from '../util/logger';
import { EOBSInputTypes } from '../util/obs_enums';

const testName = 'osn-relative-coordinates';

function makeSceneItemInfo(name: string): osn.ISceneItemInfo {
    return {
        name,
        crop: { left: 1, top: 2, right: 3, bottom: 4 },
        scaleX: 1.25,
        scaleY: 0.75,
        visible: true,
        x: 320,
        y: 180,
        rotation: 15,
        streamVisible: true,
        recordingVisible: true,
        scaleFilter: osn.EScaleType.Disable,
        blendingMode: osn.EBlendingMode.Normal,
        blendingMethod: osn.EBlendingMethod.Default,
    };
}

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed = false;

    before(function() {
        logInfo(testName, `Starting ${testName} tests`);
        deleteConfigFiles();
        obs = new OBSHandler(testName, true);
    });

    after(async function() {
        obs.shutdown();

        if (hasTestFailed) {
            logInfo(testName, 'One or more test cases failed. Uploading cache');
            await obs.uploadTestCache();
        }

        obs = null;
        deleteConfigFiles();
        logInfo(testName, `Finished ${testName} tests`);
        logEmptyLine();
    });

    afterEach(function() {
        if (this.currentTest.state === 'failed') hasTestFailed = true;
    });

    it('leaves transformed items unassigned when the canvas is omitted', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-omitted-canvas-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-omitted-canvas-source');
        const transform = makeSceneItemInfo(source.name);
        const plainItem = scene.add(source);
        const omittedItem = scene.add(source, transform);
        const undefinedItem = (scene as any).add(source, transform, undefined) as osn.ISceneItem;
        const [bulkItem] = osn.addItems(scene, [transform]);

        try {
            expect(plainItem.video).to.equal(null);
            expect(omittedItem.video).to.equal(null);
            expect(undefinedItem.video).to.equal(null);
            expect(bulkItem.video).to.equal(null);
            expect(omittedItem.position.x).to.be.closeTo(transform.x, 0.01);
            expect(omittedItem.position.y).to.be.closeTo(transform.y, 0.01);
            expect(omittedItem.scale.x).to.be.closeTo(transform.scaleX, 0.01);
            expect(omittedItem.scale.y).to.be.closeTo(transform.scaleY, 0.01);
            expect(omittedItem.crop).to.deep.equal(transform.crop);
        } finally {
            bulkItem.remove();
            undefinedItem.remove();
            omittedItem.remove();
            plainItem.remove();
            source.release();
            scene.release();
        }
    });

    it('assigns a supplied canvas before applying the initial transform', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-explicit-canvas-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-explicit-canvas-source');
        const transform = makeSceneItemInfo(source.name);
        const secondCanvas = osn.VideoFactory.create();
        const secondVideoInfo: osn.IVideoInfo = {
            ...obs.defaultVideoContext.video,
            baseWidth: 720,
            baseHeight: 1280,
            outputWidth: 720,
            outputHeight: 1280,
        };
        let item: osn.ISceneItem;

        try {
            secondCanvas.video = secondVideoInfo;
            item = scene.add(source, transform, secondCanvas);

            expect(item.video).to.not.equal(null);
            expect(item.video.video.baseWidth).to.equal(secondVideoInfo.baseWidth);
            expect(item.video.video.baseHeight).to.equal(secondVideoInfo.baseHeight);
            expect(item.position.x).to.be.closeTo(transform.x, 0.01);
            expect(item.position.y).to.be.closeTo(transform.y, 0.01);
            expect(item.scale.x).to.be.closeTo(transform.scaleX, 0.01);
            expect(item.scale.y).to.be.closeTo(transform.scaleY, 0.01);
            expect(item.crop).to.deep.equal(transform.crop);
        } finally {
            if (item) item.remove();
            source.release();
            scene.release();
            secondCanvas.destroy();
        }
    });

    it('rejects a destroyed canvas without adding a scene item', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-destroyed-canvas-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-destroyed-canvas-source');
        const transform = makeSceneItemInfo(source.name);
        const destroyedCanvas = osn.VideoFactory.create();
        let addError: Error;
        let remainingItems: osn.ISceneItem[] = [];

        try {
            destroyedCanvas.destroy();

            try {
                scene.add(source, transform, destroyedCanvas);
            } catch (error) {
                addError = error;
            }

            expect(addError).to.be.instanceOf(Error);
            expect(addError.message).to.equal('Canvas reference is not valid.');
            remainingItems = scene.getItems();
            expect(remainingItems).to.have.length(0);

            const unassignedItem = scene.add(source);
            let setterError: Error;
            try {
                unassignedItem.video = destroyedCanvas;
            } catch (error) {
                setterError = error;
            }

            expect(setterError).to.be.instanceOf(Error);
            expect(setterError.message).to.equal('Canvas reference is not valid.');
            expect(unassignedItem.video).to.equal(null);
            unassignedItem.remove();
        } finally {
            remainingItems.forEach(item => item.remove());
            source.release();
            scene.release();
        }
    });

    it('rejects invalid canvas arguments without adding scene items', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-invalid-canvas-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-invalid-canvas-source');
        const transform = makeSceneItemInfo(source.name);
        const canvas = osn.VideoFactory.create();
        const invalidCanvases = [null, 42, {}];
        let remainingItems: osn.ISceneItem[] = [];

        try {
            invalidCanvases.forEach(invalidCanvas => {
                let addError: Error;
                try {
                    (scene as any).add(source, transform, invalidCanvas);
                } catch (error) {
                    addError = error;
                }

                expect(addError).to.be.instanceOf(TypeError);
                expect(addError.message).to.equal('Video canvas must be a Video instance.');
            });

            let missingTransformError: Error;
            try {
                (scene as any).add(source, undefined, canvas);
            } catch (error) {
                missingTransformError = error;
            }

            expect(missingTransformError).to.be.instanceOf(TypeError);
            expect(missingTransformError.message).to.equal('A transform is required when a video canvas is provided.');

            let setterError: Error;
            const item = scene.add(source);
            try {
                (item as any).video = {};
            } catch (error) {
                setterError = error;
            }

            expect(setterError).to.be.instanceOf(TypeError);
            expect(setterError.message).to.equal('Video canvas must be a Video instance.');
            item.remove();
            remainingItems = scene.getItems();
            expect(remainingItems).to.have.length(0);
        } finally {
            remainingItems.forEach(item => item.remove());
            source.release();
            scene.release();
            canvas.destroy();
        }
    });

    it('preserves absolute appearance when assigning an item to another canvas', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-rebase-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-rebase-source');
        const item = scene.add(source);
        const secondCanvas = osn.VideoFactory.create();
        const secondVideoInfo: osn.IVideoInfo = {
            ...obs.defaultVideoContext.video,
            baseWidth: 1920,
            baseHeight: 1080,
            outputWidth: 1920,
            outputHeight: 1080,
        };

        try {
            secondCanvas.video = secondVideoInfo;
            item.position = { x: 320, y: 180 };
            item.scale = { x: 1.25, y: 0.75 };
            item.bounds = { x: 400, y: 200 };
            item.crop = { left: 1, top: 2, right: 3, bottom: 4 };

            item.video = secondCanvas;

            expect(item.position.x).to.be.closeTo(320, 0.01);
            expect(item.position.y).to.be.closeTo(180, 0.01);
            expect(item.scale.x).to.be.closeTo(1.25, 0.01);
            expect(item.scale.y).to.be.closeTo(0.75, 0.01);
            expect(item.bounds.x).to.be.closeTo(400, 0.01);
            expect(item.bounds.y).to.be.closeTo(200, 0.01);
            expect(item.crop).to.deep.equal({ left: 1, top: 2, right: 3, bottom: 4 });
        } finally {
            item.remove();
            source.release();
            scene.release();
            secondCanvas.destroy();
        }
    });

    it('rejects destroying an assigned canvas and keeps the canvas usable for reassignment', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-canvas-lifetime-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-canvas-lifetime-source');
        const item = scene.add(source);
        const secondCanvas = osn.VideoFactory.create();
        const secondVideoInfo: osn.IVideoInfo = {
            ...obs.defaultVideoContext.video,
            baseWidth: 720,
            baseHeight: 1280,
            outputWidth: 720,
            outputHeight: 1280,
        };
        const authoredPosition = { x: 180, y: 320 };
        let secondCanvasDestroyed = false;
        let destroyError: Error;

        try {
            secondCanvas.video = secondVideoInfo;
            item.video = secondCanvas;
            item.position = authoredPosition;

            try {
                secondCanvas.destroy();
                secondCanvasDestroyed = true;
            } catch (error) {
                destroyError = error;
            }

            expect(destroyError).to.be.instanceOf(Error);
            expect(destroyError.message).to.equal(
                'Cannot remove video context while scene items are assigned to it.',
            );

            // A rejected destroy must leave both the OSN wrapper and the
            // native item assignment intact so the caller can recover.
            expect(secondCanvas.video.baseWidth).to.equal(secondVideoInfo.baseWidth);
            expect(secondCanvas.video.baseHeight).to.equal(secondVideoInfo.baseHeight);
            expect(item.video.video.baseWidth).to.equal(secondVideoInfo.baseWidth);
            expect(item.video.video.baseHeight).to.equal(secondVideoInfo.baseHeight);
            expect(item.position.x).to.be.closeTo(authoredPosition.x, 0.01);
            expect(item.position.y).to.be.closeTo(authoredPosition.y, 0.01);

            item.video = obs.defaultVideoContext;

            expect(item.video.video.baseWidth).to.equal(obs.defaultVideoContext.video.baseWidth);
            expect(item.video.video.baseHeight).to.equal(obs.defaultVideoContext.video.baseHeight);
            expect(item.position.x).to.be.closeTo(authoredPosition.x, 0.01);
            expect(item.position.y).to.be.closeTo(authoredPosition.y, 0.01);
            secondCanvas.destroy();
            secondCanvasDestroyed = true;
        } finally {
            item.remove();
            if (!secondCanvasDestroyed) secondCanvas.destroy();
            source.release();
            scene.release();
        }
    });

    it('keeps fixed-size scene transforms independent of the assigned canvas size', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-fixed-size-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-fixed-size-source');
        const secondCanvas = osn.VideoFactory.create();
        const originalVideoInfo: osn.IVideoInfo = {
            ...obs.defaultVideoContext.video,
            baseWidth: 1920,
            baseHeight: 1080,
            outputWidth: 1920,
            outputHeight: 1080,
        };
        const resizedVideoInfo: osn.IVideoInfo = {
            ...originalVideoInfo,
            baseWidth: 1280,
            baseHeight: 720,
            outputWidth: 1280,
            outputHeight: 720,
        };
        const authoredPosition = { x: 320, y: 180 };
        const authoredScale = { x: 1.25, y: 0.75 };
        let item: osn.ISceneItem;

        try {
            scene.update({ custom_size: true, cx: 640, cy: 360 });
            scene.load();
            expect(scene.source.width).to.equal(640);
            expect(scene.source.height).to.equal(360);

            secondCanvas.video = originalVideoInfo;
            item = scene.add(source);
            item.video = secondCanvas;
            item.position = authoredPosition;
            item.scale = authoredScale;

            secondCanvas.video = resizedVideoInfo;
            osn.SceneFactory.invalidateItemTransformCache();

            expect(scene.source.width).to.equal(640);
            expect(scene.source.height).to.equal(360);
            expect(item.position.x).to.be.closeTo(authoredPosition.x, 0.01);
            expect(item.position.y).to.be.closeTo(authoredPosition.y, 0.01);
            expect(item.scale.x).to.be.closeTo(authoredScale.x, 0.01);
            expect(item.scale.y).to.be.closeTo(authoredScale.y, 0.01);
        } finally {
            if (item) item.remove();
            source.release();
            scene.release();
            secondCanvas.destroy();
        }
    });

    it('refreshes cached absolute transforms after a relative canvas reset', () => {
        const originalVideoInfo = obs.defaultVideoContext.video;
        const resizedBaseWidth = Math.round(originalVideoInfo.baseWidth * 1.5);
        const resizedBaseHeight = Math.round(originalVideoInfo.baseHeight * 1.5);
        const resizeFactor = resizedBaseHeight / originalVideoInfo.baseHeight;
        const scene = osn.SceneFactory.create('relative-coordinate-resize-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-resize-source');
        const item = scene.add(source);

        try {
            item.video = obs.defaultVideoContext;
            item.position = { x: 320, y: 180 };
            item.scale = { x: 1, y: 1 };
            item.bounds = { x: 400, y: 200 };
            item.crop = { left: 1, top: 2, right: 3, bottom: 4 };

            // Prime the client-side caches before changing the canvas dimensions.
            expect(item.position.x).to.be.closeTo(320, 0.01);
            expect(item.scale.x).to.be.closeTo(1, 0.01);
            expect(item.crop.left).to.equal(1);

            obs.defaultVideoContext.video = {
                ...originalVideoInfo,
                baseWidth: resizedBaseWidth,
                baseHeight: resizedBaseHeight,
                outputWidth: resizedBaseWidth,
                outputHeight: resizedBaseHeight,
            };
            osn.SceneFactory.invalidateItemTransformCache();

            expect(item.position.x).to.be.closeTo(320 * resizeFactor, 0.01);
            expect(item.position.y).to.be.closeTo(180 * resizeFactor, 0.01);
            expect(item.scale.x).to.be.closeTo(resizeFactor, 0.01);
            expect(item.scale.y).to.be.closeTo(resizeFactor, 0.01);
            expect(item.bounds.x).to.be.closeTo(400 * resizeFactor, 0.01);
            expect(item.bounds.y).to.be.closeTo(200 * resizeFactor, 0.01);
            expect(item.crop).to.deep.equal({ left: 1, top: 2, right: 3, bottom: 4 });
        } finally {
            obs.defaultVideoContext.video = originalVideoInfo;
            osn.SceneFactory.invalidateItemTransformCache();
            item.remove();
            source.release();
            scene.release();
        }
    });

    it('does not suppress writes that match stale cached transforms after invalidation', () => {
        const originalVideoInfo = obs.defaultVideoContext.video;
        const resizedVideoInfo: osn.IVideoInfo = {
            ...originalVideoInfo,
            baseWidth: Math.round(originalVideoInfo.baseWidth * 1.5),
            baseHeight: Math.round(originalVideoInfo.baseHeight * 1.5),
            outputWidth: Math.round(originalVideoInfo.outputWidth * 1.5),
            outputHeight: Math.round(originalVideoInfo.outputHeight * 1.5),
        };
        const scene = osn.SceneFactory.create('relative-coordinate-stale-cache-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-stale-cache-source');
        const item = scene.add(source);
        const originalPosition = { x: 320, y: 180 };
        const originalScale = { x: 1, y: 1 };
        const originalCrop = { left: 1, top: 2, right: 3, bottom: 4 };

        try {
            item.video = obs.defaultVideoContext;
            item.position = originalPosition;
            item.scale = originalScale;
            item.crop = originalCrop;

            // Prime the individual-property caches with the values that will be
            // written again after the native relative transform changes.
            expect(item.position.x).to.be.closeTo(originalPosition.x, 0.01);
            expect(item.scale.x).to.be.closeTo(originalScale.x, 0.01);
            expect(item.crop).to.deep.equal(originalCrop);

            obs.defaultVideoContext.video = resizedVideoInfo;
            osn.SceneFactory.invalidateItemTransformCache();

            // These values equal the stale cache entries but not the resized
            // native transform, so both assignments must cross the bridge.
            item.position = originalPosition;
            item.scale = originalScale;
            item.crop = originalCrop;

            expect(item.position.x).to.be.closeTo(originalPosition.x, 0.01);
            expect(item.position.y).to.be.closeTo(originalPosition.y, 0.01);
            expect(item.scale.x).to.be.closeTo(originalScale.x, 0.01);
            expect(item.scale.y).to.be.closeTo(originalScale.y, 0.01);
            expect(item.crop).to.deep.equal(originalCrop);
        } finally {
            obs.defaultVideoContext.video = originalVideoInfo;
            osn.SceneFactory.invalidateItemTransformCache();
            item.remove();
            source.release();
            scene.release();
        }
    });

    it('refreshes only items assigned to the resized canvas', () => {
        const originalVideoInfo = obs.defaultVideoContext.video;
        const firstCanvasInfo: osn.IVideoInfo = {
            ...originalVideoInfo,
            baseWidth: originalVideoInfo.baseWidth,
            baseHeight: originalVideoInfo.baseHeight,
            outputWidth: originalVideoInfo.baseWidth,
            outputHeight: originalVideoInfo.baseHeight,
        };
        const secondCanvasInfo: osn.IVideoInfo = {
            ...originalVideoInfo,
            baseWidth: originalVideoInfo.baseWidth * 2,
            baseHeight: originalVideoInfo.baseHeight * 2,
            outputWidth: originalVideoInfo.baseWidth * 2,
            outputHeight: originalVideoInfo.baseHeight * 2,
        };
        const resizedFirstCanvasInfo: osn.IVideoInfo = {
            ...firstCanvasInfo,
            baseWidth: Math.round(firstCanvasInfo.baseWidth * 1.5),
            baseHeight: Math.round(firstCanvasInfo.baseHeight * 1.5),
            outputWidth: Math.round(firstCanvasInfo.outputWidth * 1.5),
            outputHeight: Math.round(firstCanvasInfo.outputHeight * 1.5),
        };
        const resizeFactor = resizedFirstCanvasInfo.baseHeight / firstCanvasInfo.baseHeight;
        const scene = osn.SceneFactory.create('relative-coordinate-multi-canvas-scene');
        const firstSource = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-first-canvas-source');
        const secondSource = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-second-canvas-source');
        const firstItem = scene.add(firstSource);
        const secondItem = scene.add(secondSource);
        const firstCanvas = osn.VideoFactory.create();
        const secondCanvas = osn.VideoFactory.create();

        try {
            firstCanvas.video = firstCanvasInfo;
            secondCanvas.video = secondCanvasInfo;
            firstItem.video = firstCanvas;
            secondItem.video = secondCanvas;

            firstItem.position = { x: 200, y: 100 };
            firstItem.scale = { x: 1, y: 1 };
            firstItem.bounds = { x: 300, y: 150 };
            secondItem.position = { x: 200, y: 100 };
            secondItem.scale = { x: 1, y: 1 };
            secondItem.bounds = { x: 300, y: 150 };

            // Prime both client-side caches before resizing only the first canvas.
            expect(firstItem.position.x).to.be.closeTo(200, 0.01);
            expect(secondItem.position.x).to.be.closeTo(200, 0.01);
            firstCanvas.video = resizedFirstCanvasInfo;
            osn.SceneFactory.invalidateItemTransformCache();

            expect(firstItem.position.x).to.be.closeTo(200 * resizeFactor, 0.01);
            expect(firstItem.position.y).to.be.closeTo(100 * resizeFactor, 0.01);
            expect(firstItem.scale.x).to.be.closeTo(resizeFactor, 0.01);
            expect(firstItem.scale.y).to.be.closeTo(resizeFactor, 0.01);
            expect(firstItem.bounds.x).to.be.closeTo(300 * resizeFactor, 0.01);
            expect(firstItem.bounds.y).to.be.closeTo(150 * resizeFactor, 0.01);

            expect(secondItem.position.x).to.be.closeTo(200, 0.01);
            expect(secondItem.position.y).to.be.closeTo(100, 0.01);
            expect(secondItem.scale.x).to.be.closeTo(1, 0.01);
            expect(secondItem.scale.y).to.be.closeTo(1, 0.01);
            expect(secondItem.bounds.x).to.be.closeTo(300, 0.01);
            expect(secondItem.bounds.y).to.be.closeTo(150, 0.01);
        } finally {
            firstItem.remove();
            secondItem.remove();
            firstSource.release();
            secondSource.release();
            scene.release();
            firstCanvas.destroy();
            secondCanvas.destroy();
        }
    });

    it('preserves nested-scene crop references through canvas assignment and save/load', () => {
        const outerScene = osn.SceneFactory.create('relative-coordinate-crop-outer-scene');
        const nestedScene = osn.SceneFactory.create('relative-coordinate-crop-nested-scene');
        const nestedSource = nestedScene.source;
        const verticalCanvas = osn.VideoFactory.create();
        const verticalVideoInfo: osn.IVideoInfo = {
            ...obs.defaultVideoContext.video,
            baseWidth: 720,
            baseHeight: 1280,
            outputWidth: 720,
            outputHeight: 1280,
        };
        const resizedVerticalVideoInfo: osn.IVideoInfo = {
            ...verticalVideoInfo,
            baseWidth: 1080,
            baseHeight: 1920,
            outputWidth: 1080,
            outputHeight: 1920,
        };
        const authoredCrop: osn.ICropInfo = {
            left: 12,
            top: 24,
            right: 36,
            bottom: 48,
            referenceWidth: verticalVideoInfo.baseWidth,
            referenceHeight: verticalVideoInfo.baseHeight,
        };
        const transform: osn.ISceneItemInfo = {
            name: nestedSource.name,
            crop: authoredCrop,
            scaleX: 1,
            scaleY: 1,
            visible: true,
            x: 0,
            y: 0,
            rotation: 0,
            streamVisible: true,
            recordingVisible: true,
            scaleFilter: osn.EScaleType.Disable,
            blendingMode: osn.EBlendingMode.Normal,
            blendingMethod: osn.EBlendingMethod.Default,
        };
        let item: osn.ISceneItem;

        try {
            verticalCanvas.video = verticalVideoInfo;
            item = outerScene.add(nestedSource, transform, verticalCanvas);

            expect(item.video.video.baseWidth).to.equal(verticalVideoInfo.baseWidth);
            expect(item.video.video.baseHeight).to.equal(verticalVideoInfo.baseHeight);
            expect(item.crop).to.deep.equal(authoredCrop);

            outerScene.save();
            const savedItem = (outerScene.settings as any).items[0];
            expect(savedItem.crop_ref_width).to.equal(verticalVideoInfo.baseWidth);
            expect(savedItem.crop_ref_height).to.equal(verticalVideoInfo.baseHeight);

            verticalCanvas.video = resizedVerticalVideoInfo;
            osn.SceneFactory.invalidateItemTransformCache();
            outerScene.load();
            outerScene.save();

            const reloadedItem = (outerScene.settings as any).items[0];
            expect(reloadedItem.crop_ref_width).to.equal(verticalVideoInfo.baseWidth);
            expect(reloadedItem.crop_ref_height).to.equal(verticalVideoInfo.baseHeight);
        } finally {
            if (item) {
                item.video = obs.defaultVideoContext;
                item.remove();
            }
            nestedSource.release();
            outerScene.release();
            nestedScene.release();
            verticalCanvas.destroy();
        }
    });

    it('round-trips bounds and crop-to-bounds through individual and aggregate setters', () => {
        const scene = osn.SceneFactory.create('relative-coordinate-transform-scene');
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'relative-coordinate-transform-source');
        const item = scene.add(source);

        try {
            item.bounds = { x: 640, y: 360 };
            expect(item.bounds.x).to.be.closeTo(640, 0.01);
            expect(item.bounds.y).to.be.closeTo(360, 0.01);

            item.cropToBounds = true;
            expect(item.cropToBounds).to.equal(true);

            item.transformInfo = {
                ...item.transformInfo,
                bounds: { x: 320, y: 180 },
                cropToBounds: false,
            };

            expect(item.transformInfo.bounds.x).to.be.closeTo(320, 0.01);
            expect(item.transformInfo.bounds.y).to.be.closeTo(180, 0.01);
            expect(item.transformInfo.cropToBounds).to.equal(false);
            expect(item.cropToBounds).to.equal(false);
        } finally {
            item.remove();
            source.release();
            scene.release();
        }
    });
});
