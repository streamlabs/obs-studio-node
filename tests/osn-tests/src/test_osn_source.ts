import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ISettings } from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { EOBSInputTypes, EOBSFilterTypes, EOBSTransitionTypes } from '../util/obs_enums'
import { deleteConfigFiles } from '../util/general';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';

const testName = 'osn-source';

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

    it('Get all osn-source info from all input types', () => {
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            // Creating input source
            const input = osn.InputFactory.create(inputType, 'input');

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));

            // Getting input id
            let id = undefined;
            id = input.id;

            // Checking if id was returned correctly
            expect(id).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceId,inputType));
            expect(id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));

            // Getting input name
            let inputName = undefined;
            inputName = input.name;

            // Checking if name was returned correctly
            expect(inputName).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceName, inputType));
            expect(inputName).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

            // Getting input configurable value
            let configurableValue = undefined;
            configurableValue = input.configurable;

            // Checking if configurable value was returned properly
            expect(configurableValue).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Configurable, inputType));

            // Getting input property
            let properties = undefined;
            properties = input.properties;

            // Checking if properties were returned properly
            expect(properties).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Properties, inputType));

            // Getting input settings
            let settings = undefined;
            settings = input.settings;

            // Checking if settings were returned properly
            expect(settings).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Settings, inputType));

            // Getting output flags
            let outputFlags = undefined;
            outputFlags = input.outputFlags;

            // Checking if output flags were returned properly
            expect(outputFlags).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.OutputFlags, inputType));

            input.release();
        });
    });

    it('Get all osn-source info from all filter types', () => {
        obs.filterTypes.forEach(function(filterType) {
            // Creating filter
            const filter = osn.FilterFactory.create(filterType, 'filter');

            // Checking if filter source was created correctly
            expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));

            // Getting filter id
            let id = undefined;
            id = filter.id;

            // Checking if id was returned correctly
            expect(id).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceId, filterType));
            expect(id).to.equal(filterType, GetErrorMessage(ETestErrorMsg.FilterId, filterType));

            // Getting filter name
            let filterName = undefined;
            filterName = filter.name;

            // Checking if name was returned correctly
            expect(filterName).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceName, filterType));
            expect(filterName).to.equal('filter', GetErrorMessage(ETestErrorMsg.FilterName, filterType));

            // Getting filter configurable value
            let configurableValue = undefined;
            configurableValue = filter.configurable;

            // Checking if configurable value was returned properly
            expect(configurableValue).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Configurable, filterType));

            // Getting filter property
            let properties = undefined;
            properties = filter.properties;

            // Checking if properties were returned properly
            expect(properties).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Properties, filterType));

            // Getting filter settings
            let settings = undefined;
            settings = filter.settings;

            // Checking if settings were returned properly
            expect(settings).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Settings, filterType));

            // Getting output flags
            let outputFlags = undefined;
            outputFlags = filter.outputFlags;

            // Checking if output flags were returned properly
            expect(outputFlags).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.OutputFlags, filterType));

            filter.release();
        });
    });

    it('Get all osn-source info from all transition types', () => {
        obs.transitionTypes.forEach(function(transitionType) {
            // Creating transition
            const transition = osn.TransitionFactory.create(transitionType, 'transition');

            // Checking if transition was created correctly
            expect(transition).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateTransition, transitionType));

            // Getting transition id
            let id = undefined;
            id = transition.id;

            // Checking if id was returned correctly
            expect(id).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceId, transitionType));
            expect(id).to.equal(transitionType, GetErrorMessage(ETestErrorMsg.TransitionId, transitionType));

            // Getting transition name
            let transitionName = undefined;
            transitionName = transition.name;

            // Checking if name was returned correctly
            expect(transitionName).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceName, transitionType));
            expect(transitionName).to.equal('transition', GetErrorMessage(ETestErrorMsg.TransitionName, transitionType));

            // Getting transition configurable value
            let configurableValue = undefined;
            configurableValue = transition.configurable;

            // Checking if configurable value was returned properly
            expect(configurableValue).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Configurable, transitionType));

            // Getting transition property
            let properties = undefined;
            properties = transition.properties;

            // Checking if properties were returned properly
            expect(properties).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Properties, transitionType));

            // Getting transition settings
            let settings = undefined;
            settings = transition.settings;

            // Checking if settings were returned properly
            expect(settings).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Settings, transitionType));

            // Getting output flags
            let outputFlags = undefined;
            outputFlags = transition.outputFlags;

            // Checking if output flags were returned properly
            expect(outputFlags).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.OutputFlags, transitionType));

            transition.release();
        });
    });

    it('Get all osn-source info of a scene', () => {
        const sceneName = 'test_osn_scene';

        // Creating scene
        const scene = osn.SceneFactory.create('test_osn_scene'); 

        // Checking if scene was created correctly
        expect(scene).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateScene, sceneName));

        // Getting scene id
        let id = undefined;
        id = scene.id;

        // Checking if id was returned correctly
        expect(id).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceId, sceneName));
        expect(id).to.equal('scene', GetErrorMessage(ETestErrorMsg.SceneType, sceneName));

        // Getting scene name
        let returnedSceneName = undefined;
        returnedSceneName = scene.name;

        // Checking if name was returned correctly
        expect(returnedSceneName).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.SourceName, sceneName));
        expect(returnedSceneName).to.equal('test_osn_scene', GetErrorMessage(ETestErrorMsg.SceneName, sceneName));

        // Getting scene configurable value
        let configurableValue = undefined;
        configurableValue = scene.configurable;

        // Checking if configurable value was returned properly
        expect(configurableValue).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Configurable, sceneName));

        // Getting scene properties
        let properties = undefined;
        properties = scene.properties;

        // Checking if properties were returned properly
        expect(properties).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Properties, sceneName));

        // Getting scene settings
        let settings = undefined;
        settings = scene.settings;

        // Checking if settings were returned properly
        expect(settings).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Settings, sceneName));

        // Getting output flags
        let outputFlags = undefined;
        outputFlags = scene.outputFlags;

        // Checking if output flags were returned properly
        expect(outputFlags).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.OutputFlags, sceneName));

        scene.release();
    });

    it('Update settings of all inputs', () => {
        let settings: ISettings = {};

        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            // Creating input source
            const input = osn.InputFactory.create(inputType, 'input');

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

            // Preparing settings object
            settings = input.settings;

            switch(inputType) {
                case EOBSInputTypes.ImageSource: {
                    settings['unload'] = true;
                    break;
                }
                case EOBSInputTypes.ColorSource: {
                    settings['height'] = 500;
                    settings['width'] = 600;
                    break;
                }
                case EOBSInputTypes.Slideshow: {
                    settings['loop'] = false;
                    settings['slide_time'] = 9000;
                    settings['transition_speed'] = 800;
                    break;
                }
                case EOBSInputTypes.BrowserSource: {
                    settings['fps'] = 60;
                    settings['height'] = 500;
                    settings['restart_when_active'] = true;
                    settings['shutdown'] = true;
                    break;
                }
                case EOBSInputTypes.FFMPEGSource: {
                    settings['buffering_mb'] = 3;
                    settings['clear_on_media_end'] = false;
                    settings['looping'] = true;
                    settings['speed_percent'] = 80;
                    settings['caching'] = true;
                    break;
                }
                case EOBSInputTypes.TextGDI: {
                    settings['align'] = 'right';
                    settings['extents_cx'] = 90;
                    settings['extents_cy'] = 90;
                    settings['valign'] = 'bottom';
                    break;
                }
                case EOBSInputTypes.TextFT2: {
                    settings['color1'] = 4294967296;
                    settings['color2'] = 5294967295;
                    break;
                }
                case EOBSInputTypes.MonitorCapture: {
                    settings['capture_cursor'] = false;
                    settings['monitor'] = 1;
                    break;
                }
                case EOBSInputTypes.WindowCapture: {
                    settings['compatibility'] = true;
                    settings['cursor'] = false;
                    break;
                }
                case EOBSInputTypes.GameCapture: {
                    settings['allow_transparency'] = true;
                    settings['force_scaling'] = true;
                    settings['hook_rate'] = 2;
                    break;
                }
                case EOBSInputTypes.DShowInput: {
                    settings['audio_output_mode'] = 1;
                    settings['res_type'] = 1;
                    settings['video_format'] = 2;
                    break;
                }
                case EOBSInputTypes.WASAPIInput: {
                    settings['use_device_timing'] = true;
                    break;
                }
                case EOBSInputTypes.WASAPIOutput: {
                    settings['use_device_timing'] = false;
                    break;
                }
            }

            // Updating settings of source
            input.update(settings);

            // Sending save signal to source
            expect(function() {
                input.save();
            }).to.not.throw();

            // Checking if setting was added to source
            expect(input.settings).to.eql(settings, GetErrorMessage(ETestErrorMsg.SaveSettings, inputType));

            settings = {};
            input.release();
        });
    });

    it('Update settings of all filters', () => {
        let settings: ISettings = {};

        obs.filterTypes.forEach(function(filterType) {
            if (filterType != EOBSFilterTypes.GPUDelay &&
                filterType != EOBSFilterTypes.AsyncDelay &&
                filterType != EOBSFilterTypes.InvertPolarity &&
                filterType != EOBSFilterTypes.PremultipliedAlpha &&
                filterType != EOBSFilterTypes.VST) {
                // Creating filter
                const filter = osn.FilterFactory.create(filterType, 'filter');
    
                // Checking if filter source was created correctly
                expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));
                expect(filter.id).to.equal(filterType, GetErrorMessage(ETestErrorMsg.FilterId, filterType));
                expect(filter.name).to.equal('filter', GetErrorMessage(ETestErrorMsg.FilterName, filterType));

                // Preparing settings object
                settings = filter.settings;

                switch(filterType) {
                    case EOBSFilterTypes.Mask: {
                        settings['color'] = 26777215;
                        settings['opacity'] = 80;
                        break;
                    }
                    case EOBSFilterTypes.Crop: {
                        settings['relative'] = false;
                        break;
                    }
                    case EOBSFilterTypes.Gain: {
                        settings['db'] = 5;
                        break;
                    }
                    case EOBSFilterTypes.Color: {
                        settings['brightness'] = 10;
                        settings['contrast'] = 15;
                        settings['saturation'] = 55;
                        break;
                    }
                    case EOBSFilterTypes.Scale: {
                        settings['sampling'] = 'bilinear';
                        settings['undistort'] = true;
                        break;
                    }
                    case EOBSFilterTypes.Scroll: {
                        settings['cx'] = 80;
                        settings['cy'] = 60;
                        settings['limit_size'] = true;
                        break;
                    }
                    case EOBSFilterTypes.ColorKey: {
                        settings['gamma'] = 60;
                        settings['key_color_type'] = 'red';
                        settings['similarity'] = 55;
                        break;
                    }
                    case EOBSFilterTypes.Clut: {
                        settings['clut_amount'] = 2;
                        break;
                    }
                    case EOBSFilterTypes.Sharpness: {
                        settings['sharpness'] = 0.05;
                        break;
                    }
                    case EOBSFilterTypes.ChromaKey: {
                        settings['key_color'] = 68280;
                        settings['smoothness'] = 20;
                        settings['spill'] = 90;
                        break;
                    }
                    case EOBSFilterTypes.NoiseSuppress: {
                        settings['suppress_level'] = -10;
                        settings['']
                        break;
                    }
                    case EOBSFilterTypes.NoiseGate: {
                        settings['hold_time'] = 120;
                        settings['open_threshold'] = -12;
                        settings['release_time'] = 80;
                        break;
                    }
                    case EOBSFilterTypes.Compressor: {
                        settings['attack_time'] = 8;
                        settings['output_gain'] = 4;
                        settings['threshold'] = -9;
                        break;
                    }
                    case EOBSFilterTypes.Limiter: {
                        settings['release_time'] = 30;
                        settings['threshold'] = -12;
                        break;
                    }
                    case EOBSFilterTypes.Expander: {
                        settings['attack_time'] = 15;
                        settings['ratio'] = 5;
                        settings['threshold'] = -20;
                        break;
                    }
                    case EOBSFilterTypes.NDI:
                    case EOBSFilterTypes.NDIAudio: {
                        settings['ndi_filter_name'] = 'Test Filter';
                    }
                }
    
                // Updating settings of filter
                filter.update(settings);

                // Sending save signal to filter
                expect(function() {
                    filter.save();
                }).to.not.throw;

                // Checking if setting was added to filter
                expect(filter.settings).to.eql(settings, GetErrorMessage(ETestErrorMsg.SaveSettings, filterType));
    
                filter.release();
            }
        });
    });

    it('Update settings of all transitions', () => {
        let settings: ISettings = {};

        obs.transitionTypes.forEach(function(transitionType) {
            if(transitionType == EOBSTransitionTypes.FadeToColor ||
                transitionType == EOBSTransitionTypes.Wipe) {
                // Creating filter
                const transition = osn.FilterFactory.create(transitionType, 'transition');
    
                // Checking if filter source was created correctly
                expect(transition).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateTransition, transitionType));
                expect(transition.id).to.equal(transitionType, GetErrorMessage(ETestErrorMsg.TransitionId, transitionType));
                expect(transition.name).to.equal('transition', GetErrorMessage(ETestErrorMsg.TransitionName, transitionType));

                // Preparing settings object
                settings = transition.settings;

                switch(transitionType) {
                    case EOBSTransitionTypes.FadeToColor: {
                        settings['color'] = 5278190080;
                        settings['switch_point'] = 80;
                        break;
                    }
                    case EOBSTransitionTypes.Wipe: {
                        settings['luma_invert'] = true;
                        settings['luma_softness'] = 0.05;
                        break;
                    }
                }
    
                // Updating settings of transition
                transition.update(settings);

                // Sending save signal to transition
                expect(function() {
                    transition.save();
                }).to.not.throw();

                // Checking if setting was added to transition
                expect(transition.settings).to.include(settings, GetErrorMessage(ETestErrorMsg.SaveSettings, transitionType));
    
                transition.release();
            }
        });
    });

    it('Set flags and get them for all input source types', () => {
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            // Creating input source
            const input = osn.InputFactory.create(inputType, 'input');

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

            // Setting input source flags
            input.flags = osn.ESourceFlags.ForceMono;

            // Getting input source flags
            const flags = input.flags;

            // Checking if flags were returned correctly
            expect(flags).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Flags, inputType));
            expect(flags).to.equal(osn.ESourceFlags.ForceMono, GetErrorMessage(ETestErrorMsg.FlagsWrongValue, inputType));

            input.release();
        });
    });

    it('Set muted and get it for all input source types', () => {
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            // Creating input source
            const input = osn.InputFactory.create(inputType, 'input');

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

            // Setting input source flags
            input.muted = true;

            // Getting input source flags
            const muted = input.muted;

            // Checking if flags were returned correctly
            expect(muted).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Muted, inputType));
            expect(muted).to.equal(true, GetErrorMessage(ETestErrorMsg.MutedWrongValue, inputType));

            input.release();
        });
    });

    it('Set enabled and get it for all filter types', () => {
        obs.filterTypes.forEach(function(filterType) {
            logInfo(testName, 'Testing filter type: ' + filterType);
            // Creating filter
            const filter = osn.FilterFactory.create(filterType, 'filter');

            // Checking if filter source was created correctly
            expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));
            expect(filter.id).to.equal(filterType, GetErrorMessage(ETestErrorMsg.FilterId, filterType));
            expect(filter.name).to.equal('filter', GetErrorMessage(ETestErrorMsg.FilterName, filterType));

            // Setting enabled
            filter.enabled = true;

            // Getting enabled
            const enabled = filter.enabled;

            // Checking if enabled was returnd correctly
            expect(enabled).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Enabled, filterType));
            expect(enabled).to.equal(true, GetErrorMessage(ETestErrorMsg.EnabledWrongValue, filterType));

            filter.release();
        });
    });
});
