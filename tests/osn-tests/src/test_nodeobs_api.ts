import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler, IPerformanceState, TOBSHotkey } from '../util/obs_handler';
import { showHideInputHotkeys, slideshowHotkeys, ffmpeg_sourceHotkeys,
    game_captureHotkeys, dshow_wasapitHotkeys,coreaudioHotkeys,  deleteConfigFiles } from '../util/general';

const testName = 'nodeobs_api';

describe(testName, function() {
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

    it('Get performance statistics', function() {
        let stats: IPerformanceState;

        // Getting performance statistics
        stats = osn.NodeObs.OBS_API_getPerformanceStatistics();

        // Checking if performance statistics values returned correctly
        expect(stats.CPU).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'CPU'));
        expect(stats.numberDroppedFrames).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'numberDroppedFrames'));
        expect(stats.percentageDroppedFrames).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'percentageDroppedFrames'));
        expect(stats.streamingBandwidth).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'streamingBandwidth'));
        expect(stats.streamingDataOutput).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'streamingDataOutput'));
        expect(stats.recordingBandwidth).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'recordingBandwidth'));
        expect(stats.recordingDataOutput).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'recordingDataOutput'));
        expect(stats.frameRate).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'frameRate'));
        expect(stats.averageTimeToRenderFrame).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'averageTimeToRenderFrame'));
        expect(stats.memoryUsage).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'memoryUsage'));
        expect(stats.diskSpaceAvailable).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetPerformanceStatistics, 'diskSpaceAvailable'));
    });

    it('Get hotkeys of all sources and process them', function() {
        let obsHotkeys: TOBSHotkey[];

        // Creating scene
        const sceneName = 'hotkeys_test_scene';
        const scene = osn.SceneFactory.create(sceneName);

        // Checking if scene was created correctly
        expect(scene).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateScene, sceneName));
        expect(scene.id).to.equal('scene', GetErrorMessage(ETestErrorMsg.SceneId, sceneName));
        expect(scene.name).to.equal(sceneName, GetErrorMessage(ETestErrorMsg.SceneName, sceneName));
        expect(scene.type).to.equal(osn.ESourceType.Scene, GetErrorMessage(ETestErrorMsg.SceneType, sceneName));

        obs.inputTypes.forEach(inputType => {
            if(obs.skipSource(inputType)) { return;}
            // Creating source
            const input = osn.InputFactory.create(inputType, inputType);

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputName, inputType));

            // Adding input source to scene
            const sceneItem = scene.add(input);

            // Checking if input source was added to the scene correctly
            expect(sceneItem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.AddSourceToScene, inputType, sceneName));
            expect(sceneItem.source.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.SceneItemInputId, inputType));
            expect(sceneItem.source.name).to.equal(inputType, GetErrorMessage(ETestErrorMsg.SceneItemInputName, inputType));
        });

        // Getting all hotkeys
        obsHotkeys = osn.NodeObs.OBS_API_QueryHotkeys();

        // Check if hotkeys exists and process them
        obsHotkeys.forEach(function(hotkey) {
            switch(hotkey.ObjectName) {
                case sceneName: {
                    expect(hotkey.HotkeyName).to.be.oneOf(showHideInputHotkeys, GetErrorMessage(ETestErrorMsg.ShowHideInputHotkeys));
                    break;
                }
                case 'slideshow': {
                    expect(hotkey.HotkeyName).to.be.oneOf(slideshowHotkeys, GetErrorMessage(ETestErrorMsg.SlideShowHotkeys));
                    break;
                }
                case 'ffmpeg_source': {
                    expect(hotkey.HotkeyName).to.be.oneOf(ffmpeg_sourceHotkeys, GetErrorMessage(ETestErrorMsg.FFMPEGSourceHotkeys));
                    break;
                }
                case 'game_capture': {
                    expect(hotkey.HotkeyName).to.be.oneOf(game_captureHotkeys, GetErrorMessage(ETestErrorMsg.GameCaptureHotkeys));
                    break;
                }
                case 'dshow_input': {
                    expect(hotkey.HotkeyName).to.be.oneOf(dshow_wasapitHotkeys, GetErrorMessage(ETestErrorMsg.DShowInputHotkeys));
                    break;
                }
                case 'wasapi_input_capture': {
                    expect(hotkey.HotkeyName).to.be.oneOf(dshow_wasapitHotkeys, GetErrorMessage(ETestErrorMsg.WASAPIInputHotkeys));
                    break;
                }
                case 'wasapi_output_capture': {
                    expect(hotkey.HotkeyName).to.be.oneOf(dshow_wasapitHotkeys, GetErrorMessage(ETestErrorMsg.WASAPIOutputHotkeys));
                    break;
                }
                case 'coreaudio_input_capture': {
                    expect(hotkey.HotkeyName).to.be.oneOf(coreaudioHotkeys, GetErrorMessage(ETestErrorMsg.CoreAudioInputHotkeys));
                    break;
                }
                case 'coreaudio_output_capture': {
                    expect(hotkey.HotkeyName).to.be.oneOf(coreaudioHotkeys, GetErrorMessage(ETestErrorMsg.CoreAudioOutputHotkeys));
                    break;
                }
                default: {
                    break;
                }
            }
            expect(function() {
                osn.NodeObs.OBS_API_ProcessHotkeyStatus(hotkey.HotkeyId, true);
            }).to.not.throw();
        });
        // Checking if hotkeys returned properly
        expect(obsHotkeys.length).to.not.equal(0);

        scene.getItems().forEach(function(sceneItem) {
            sceneItem.source.release();
            sceneItem.remove();
        });

        scene.release();
    });

    it('Get and set the browser source acceleration', function() {
        expect(osn.NodeObs.GetBrowserAcceleration()).
            to.equal(true, 'Invalid browser source acceleration default value');
        osn.NodeObs.SetBrowserAcceleration(false);
        expect(osn.NodeObs.GetBrowserAcceleration()).
            to.equal(false, 'Invalid browser source acceleration value');
    });

    it('Get and set media file caching', function() {
        expect(osn.NodeObs.GetMediaFileCaching()).
            to.equal(true, 'Invalid media file caching default value');
        osn.NodeObs.SetMediaFileCaching(false);
        expect(osn.NodeObs.GetMediaFileCaching()).
            to.equal(false, 'Invalid media file caching value');
    });

    it('Get and set process priority', function() {
        expect(osn.NodeObs.GetProcessPriority()).
            to.equal('Normal', 'Invalid process priority default value');
        osn.NodeObs.SetProcessPriority('High');
        expect(osn.NodeObs.GetProcessPriority()).
            to.equal('High', 'Invalid process priority value');
    });

    it('Stop crash handler', function() {
        // Stopping crash handler as a last test case
        expect(function() {
            osn.NodeObs.InitShutdownSequence();
        }).to.not.throw();
    });
});
