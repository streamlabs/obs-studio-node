import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler, IVec2, ICrop } from '../util/obs_handler';
import { EOBSInputTypes } from '../util/obs_enums';
import { deleteConfigFiles } from '../util/general';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';

const testName = 'osn-sceneitem';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    let sceneName: string = 'test_scene';
    let sourceName: string = 'test_source';

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

    beforeEach(function() {
        // Creating scene
        const scene = osn.SceneFactory.create(sceneName); 

        // Checking if scene was created correctly
        expect(scene).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateScene, sceneName));
        expect(scene.id).to.equal('scene', GetErrorMessage(ETestErrorMsg.SceneId, sceneName));
        expect(scene.name).to.equal(sceneName, GetErrorMessage(ETestErrorMsg.SceneName, sceneName));
        expect(scene.type).to.equal(osn.ESourceType.Scene, GetErrorMessage(ETestErrorMsg.SceneType, sceneName));

        // Creating input source
        const source = osn.InputFactory.create(EOBSInputTypes.ImageSource, sourceName);

        // Checking if source was created correctly
        expect(source).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, EOBSInputTypes.ImageSource));
        expect(source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.InputId, EOBSInputTypes.ImageSource));
        expect(source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.InputName, EOBSInputTypes.ImageSource));
    });

    afterEach(function() {
        const scene = osn.SceneFactory.fromName(sceneName);
        scene.release();

        if (this.currentTest.state == 'failed') {
            hasTestFailed = true;
        }
    });

    it('Get scene item id', () => {
        let sceneItemId: number = undefined;

        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Getting scene item id
        sceneItemId = sceneItem.id;

        // Checking
        expect(sceneItemId).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SceneItemId));

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Get source associated with a scene item', () => {
        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Getting source from scene item to create scene item
        const returnedSource = sceneItem.source;

        // Checking if source was returned correctly
        expect(returnedSource).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetSourceFromSceneItem, sceneItem.id.toString()));
        expect(returnedSource.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SourceFromSceneItemId, sceneItem.id.toString()));
        expect(returnedSource.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SourceFromSceneItemName, sceneItem.id.toString()));

        sceneItem.source.release();
        sceneItem.remove();
        scene.release();
    });

    it('Get scene associated with a scene item', () => {
        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Getting scene associated with scene item
        const returnedScene = sceneItem.scene;

        // Checking if scene was returned correctly
        expect(returnedScene).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetSceneFromSceneItem, sceneItem.id.toString()));
        expect(scene.id).to.equal('scene', GetErrorMessage(ETestErrorMsg.SceneFromSceneItemId, sceneItem.id.toString()));
        expect(scene.name).to.equal(sceneName, GetErrorMessage(ETestErrorMsg.SceneFromSceneItemName, sceneItem.id.toString()));
        expect(scene.type).to.equal(osn.ESourceType.Scene, GetErrorMessage(ETestErrorMsg.SceneFromSceneItemType, sceneItem.id.toString()));
        
        sceneItem.source.release();
        sceneItem.remove();
        scene.release();
    });

    it('Set scene item as visible and not visible and check it', () => {
        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Setting scene item as visible
        sceneItem.visible = true;

        // Checking if scene item is visible
        expect(sceneItem.visible).to.equal(true, GetErrorMessage(ETestErrorMsg.Visible));

        // Setting scene item as not visible
        sceneItem.visible = false;

        // Checking if scene item is not visible
        expect(sceneItem.visible).to.equal(false, GetErrorMessage(ETestErrorMsg.Visible));

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Set scene item as selected or not selected and check it', () => {
        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Setting scene item as selected
        sceneItem.selected = true;
        
        // Checking if scene item is selected
        expect(sceneItem.selected).to.equal(true, GetErrorMessage(ETestErrorMsg.Selected));

        // Setting scene item as not selected
        sceneItem.selected = false;

        // Checking if scene item is selected
        expect(sceneItem.selected).to.equal(false, GetErrorMessage(ETestErrorMsg.Selected));

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Set scene item position and get it', () => {
        let position: IVec2 = {x: 1,y: 2};

        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Setting position of scene item
        sceneItem.position = position;

        // Getting position of scene item
        const returnedPosition = sceneItem.position;

        // Checking if position was set properly
        expect(sceneItem.position.x).to.equal(returnedPosition.x, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(sceneItem.position.y).to.equal(returnedPosition.y, GetErrorMessage(ETestErrorMsg.PositionY));

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Set scene item rotation and get it', () => {
        let rotation: number = 180;

        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Setting scene item rotation
        sceneItem.rotation = rotation;

        // Getting scene item rotation
        const returnedRotation = sceneItem.rotation;

        // Checkin if rotation was set properly
        expect(sceneItem.rotation).to.equal(returnedRotation, GetErrorMessage(ETestErrorMsg.Rotation));
    
        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Set scene item scale and get it', () => {
        let scale: IVec2 = {x: 20, y:30};

        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Setting scene item scale
        sceneItem.scale = scale;

        // Getting scene item scale
        const returnedScale = sceneItem.scale;

        // Checking if scale was set properly
        expect(sceneItem.scale.x).to.equal(returnedScale.x, GetErrorMessage(ETestErrorMsg.ScaleX));
        expect(sceneItem.scale.y).to.equal(returnedScale.y, GetErrorMessage(ETestErrorMsg.ScaleY));

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Get default transform info of scene item', () => {
        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        const info = sceneItem.transformInfo;

        expect(info.pos.x).to.equal(0.0, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(info.pos.y).to.equal(0.0, GetErrorMessage(ETestErrorMsg.PositionY));
        expect(info.rot).to.equal(0.0, GetErrorMessage(ETestErrorMsg.Rotation));
        expect(info.scale.x).to.equal(1.0, GetErrorMessage(ETestErrorMsg.ScaleX));
        expect(info.scale.y).to.equal(1.0, GetErrorMessage(ETestErrorMsg.ScaleY));
        expect(info.alignment).to.equal(osn.EAlignment.TopLeft, GetErrorMessage(ETestErrorMsg.Alignment));
        expect(info.boundsType).to.equal(osn.EBoundsType.None, GetErrorMessage(ETestErrorMsg.BoundType));
        expect(info.boundsAlignment).to.equal(0, GetErrorMessage(ETestErrorMsg.BoundAlignment));
        expect(info.bounds.x).to.equal(0.0, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(info.bounds.y).to.equal(0.0, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(info.cropToBounds).to.equal(false);

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Set transform info of scene item', () => {
        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        sceneItem.transformInfo = {
            pos: { x: 10, y: 5 },
            rot: 50.5,
            scale: { x: 0.5, y: 0.7 },
            alignment: osn.EAlignment.Center,
            boundsType: osn.EBoundsType.ScaleToWidth,
            boundsAlignment: 100500,
            bounds: { x: 100, y: 200 },
            cropToBounds: true,
        };

        const info = sceneItem.transformInfo;

        expect(info.pos.x).to.be.closeTo(10, 0.001, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(info.pos.y).to.be.closeTo(5, 0.001, GetErrorMessage(ETestErrorMsg.PositionY));
        expect(info.rot).to.be.closeTo(50.5, 0.001, GetErrorMessage(ETestErrorMsg.Rotation));
        expect(info.scale.x).to.be.closeTo(0.5, 0.001, GetErrorMessage(ETestErrorMsg.ScaleX));
        expect(info.scale.y).to.be.closeTo(0.7, 0.001, GetErrorMessage(ETestErrorMsg.ScaleY));
        expect(info.alignment).to.equal(osn.EAlignment.Center, GetErrorMessage(ETestErrorMsg.Alignment));
        expect(info.boundsType).to.equal(osn.EBoundsType.ScaleToWidth, GetErrorMessage(ETestErrorMsg.BoundType));
        expect(info.boundsAlignment).to.equal(100500, GetErrorMessage(ETestErrorMsg.BoundAlignment));
        expect(info.bounds.x).to.be.closeTo(100, 0.001, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(info.bounds.y).to.be.closeTo(200, 0.001, GetErrorMessage(ETestErrorMsg.PositionX));
        expect(info.cropToBounds).to.equal(true);

        sceneItem.source.release();
        sceneItem.remove();
    });

    it('Set crop value and get it', () => {
        let crop: ICrop = {top: 5, bottom: 5, left: 3, right:3};

        // Getting scene
        const scene = osn.SceneFactory.fromName(sceneName);

        // Getting source
        const source = osn.InputFactory.fromName(sourceName);

        // Adding input source to scene to create scene item
        const sceneItem = scene.add(source);

        // Checking if input source was added to the scene correctly
        expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, EOBSInputTypes.ImageSource, sceneName));
        expect(sceneItem.source.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.SceneItemInputId, EOBSInputTypes.ImageSource));
        expect(sceneItem.source.name).to.equal(sourceName, GetErrorMessage(ETestErrorMsg.SceneItemInputName, EOBSInputTypes.ImageSource));

        // Setting crop
        sceneItem.crop = crop;

        // Getting crop
        const returnedCrop = sceneItem.crop;

        // Checking if crop was set properly
        expect(sceneItem.crop.top).to.equal(returnedCrop.top, GetErrorMessage(ETestErrorMsg.CropTop));
        expect(sceneItem.crop.bottom).to.equal(returnedCrop.bottom, GetErrorMessage(ETestErrorMsg.CropBottom));
        expect(sceneItem.crop.left).to.equal(returnedCrop.left, GetErrorMessage(ETestErrorMsg.CropLeft));
        expect(sceneItem.crop.right).to.equal(returnedCrop.right, GetErrorMessage(ETestErrorMsg.CropRight));

        sceneItem.source.release();
        sceneItem.remove();
    });
});
