import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { IInput, ISettings, ITimeSpec } from '../osn';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { EOBSInputTypes, EOBSFilterTypes } from '../util/obs_enums';
import { OBSHandler } from '../util/obs_handler';
import { getTimeSpec, deleteConfigFiles} from '../util/general';
import * as inputSettings from '../util/input_settings';

const testName = 'osn-input';

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

    it('Check list of input types', () => {
        // have to ignore vlc_source, mediasoupconnector, and ndi_source as they are not available on CI
        const ignoreInputTypes = ["vlc_source", "mediasoupconnector", "ndi_source"];
        let expectedInputTypes = ["audio_line", "image_source", "color_source", "color_source_v2", "color_source_v3", "slideshow", "mediasoupconnector", "browser_source", "ffmpeg_source", "text_ft2_source", "text_ft2_source_v2", "vlc_source", "window_capture", "slideshow_v2"];

        if (obs.os == 'darwin') {
            let additionalDarwinTypes = ["macos_avcapture", "macos_avcapture_fast", "mac_screen_capture", "sck_audio_capture", "av_capture_input","av_capture_input_v2","display_capture","coreaudio_input_capture","coreaudio_output_capture"];
            expectedInputTypes.push(...additionalDarwinTypes);
        } else if (obs.os == 'win32') {
            let additionalWin32Types = [ "text_gdiplus", "text_gdiplus_v2", "text_gdiplus_v3", "monitor_capture", "game_capture", "dshow_input", "openvr_capture", "spout_capture", "wasapi_input_capture", "wasapi_output_capture", "wasapi_process_output_capture", "screen_capture",];
            expectedInputTypes.push(...additionalWin32Types);
        }
        let missingDiff = expectedInputTypes.filter(x => !obs.inputTypes.includes(x));
        missingDiff = missingDiff.filter(x => !ignoreInputTypes.includes(x));
        let unexpectedDiff = obs.inputTypes.filter(x => !expectedInputTypes.includes(x));
        unexpectedDiff = unexpectedDiff.filter(x => !ignoreInputTypes.includes(x));
        if (missingDiff.length > 0 || unexpectedDiff.length > 0) {
            logInfo(testName, 'Unexpected input types: ' + JSON.stringify( unexpectedDiff));
            logInfo(testName, 'Missing input types: ' + JSON.stringify( missingDiff));
        }
        expect(missingDiff.length).to.equal(0, GetErrorMessage(ETestErrorMsg.InputsChanged));
        expect(unexpectedDiff.length).to.equal(0, GetErrorMessage(ETestErrorMsg.InputsChanged));
    });

    it('Create all types of input', () => {
        // Create all input sources available
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            const input = osn.InputFactory.create(inputType, 'input');

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));
            input.release();
        });
    });

    it('Create all types of input with settings parameter', () => {
        // Create all input sources available with settings parameter
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            let settings: ISettings = {};

            switch(inputType) {
                case 'image_source': {
                    settings = inputSettings.imageSource;
                    settings['unload'] = true;
                    settings['linear_alpha'] = false;
                    break;
                }
                case 'color_source': {
                    settings = inputSettings.colorSource;
                    settings['height'] = 500;
                    break;
                }
                case 'color_source_v2': {
                    settings = inputSettings.colorSource;
                    settings['height'] = 500;
                    break;
                }
                case 'color_source_v3': {
                    settings = inputSettings.colorSource;
                    settings['height'] = 500;
                    break;
                }
                case 'slideshow': {
                    settings = inputSettings.slideshow;
                    settings['loop'] = false;
                    break;
                }
                case 'browser_source': {
                    settings = inputSettings.browserSource;
                    settings['restart_when_active'] = true;
                    settings['webpage_control_level'] = 0;
                    break;
                }
                case 'ffmpeg_source': {
                    settings = inputSettings.ffmpegSource;
                    settings['speed_percent'] = 80;
                    settings['reconnect_delay_sec'] = 10;
                    settings['volume'] = 100;
                    break;
                }
                case 'ndi_source': {
                    settings = inputSettings.ndiSource;
                    settings['yuv_colorspace'] = 1;
                    if (obs.os == 'darwin')
                        settings['latency'] = 0;
                    break;
                }
                case 'text_gdiplus':
                case 'text_gdiplus_v2': {
                    settings = inputSettings.textGDIPlus;
                    settings['align'] = 'right';
                    settings['antialiasing'] = true;
                    settings['transform'] = 0;
                    break;
                }
                case 'text_ft2_source':
                case 'text_ft2_source_v2': {
                    settings = inputSettings.textFT2Source;
                    settings['log_lines'] = 5;
                    settings['antialiasing'] = true;
                    settings['drop_shadow'] = false;
                    settings['outline'] = false;
                    settings['word_wrap'] = false;
                    break;
                }
                case 'vlc_source': {
                    settings = inputSettings.vlcSource;
                    settings['shuffle'] = true;
                    break;
                }
                case 'monitor_capture': {
                    settings = inputSettings.monitorCapture;
                    settings['capture_cursor'] = false;
                    settings['method'] = 0;
                    settings['monitor_wgc'] = 0;
                    settings['compatibility'] = false;
                    settings['monitor_id'] = "DUMMY";
                    settings['force_sdr'] = false;
                    break;
                }
                case 'window_capture': {
                    if (obs.os == 'win32') {
                        settings = inputSettings.win32WindowCapture;
                        settings['compatibility'] = true;
                        settings['client_area'] = true;
                    } else if (obs.os == 'darwin') {
                        settings = inputSettings.macWindowCapture;
                        settings['show_empty_names'] = true;
                    }
                    break;
                }
                case 'game_capture': {
                    settings = inputSettings.gameCapture;
                    settings['allow_transparency'] = true;
                    settings['default_height'] = 1080;
                    settings['default_width'] = 1920;
                    settings['compat_info_visible'] = false;
                    break;
                }
                case 'dshow_input': {
                    settings = inputSettings.dshowInput;
                    settings['video_format'] = 1;
                    settings['autorotation'] = true;
                    settings['hw_decode'] = false;
                    break;
                }
                case 'wasapi_input_capture':
                case 'wasapi_output_capture': {
                    settings = inputSettings.wasapi;
                    settings['use_device_timing'] = true;
                    break;
                }
                case 'av_capture_input':
                case 'av_capture_input_v2': {
                    settings = inputSettings.avCaptureInput;
                    settings['color_space'] = 2;
                    settings['use_preset'] = false;
                    break;
                }
                case 'coreaudio_input_capture':
                case 'coreaudio_output_capture': {
                    settings = inputSettings.coreaudio;
                    break;
                }
                case 'display_capture': {
                    settings = inputSettings.displayCapture;
                    settings['show_cursor'] = false;
                }
                case 'openvr_capture': {
                    settings['cropbottom'] = 0;
                    settings['cropleft'] = 1;
                    settings['cropright'] = 1;
                    settings['croptop'] = 0;
                    settings['righteye'] = true;
                    break;
                }
                case 'screen_capture': {
                    settings = inputSettings.simpleCapture;
                    settings['default_height'] = 1080;
                    settings['default_width'] = 1920;
                    break;
                }
            }

            const input = osn.InputFactory.create(inputType, 'input', settings);

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));
            expect(input.settings).to.eql(settings, GetErrorMessage(ETestErrorMsg.InputSetting, inputType));
            input.release();
        });
    });

    it('Create an instance of an input by getting it by name', () => {
        let inputFromName: IInput;

        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            // Creating input source
            const name = 'input';
            const input = osn.InputFactory.create(inputType, name);

            // Checking if input source was created correctly
            expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
            expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
            expect(input.name).to.equal(name, GetErrorMessage(ETestErrorMsg.InputName, inputType));

            // Getting input source by its name
            inputFromName = osn.InputFactory.fromName(name);

            // Checking if returned input source is correct
            expect(inputFromName).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.InputFromName, name));
            expect(inputFromName.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.FromNameInputId, name));
            expect(inputFromName.name).to.equal(name, GetErrorMessage(ETestErrorMsg.FromNameInputName, name));
            input.release();
        });
    });

    it('Get volume value from input source', () => {
        let volume: number = undefined;

        // Creating input source
        const input = osn.InputFactory.create(EOBSInputTypes.FFMPEGSource, 'input');

        // Checking if input source was created correctly
        expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, EOBSInputTypes.FFMPEGSource));
        expect(input.id).to.equal(EOBSInputTypes.FFMPEGSource, GetErrorMessage(ETestErrorMsg.InputId, EOBSInputTypes.FFMPEGSource));
        expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, EOBSInputTypes.FFMPEGSource));

        // Getting input source volume
        volume = input.volume;

        // Checking if volume value is what was expected
        expect(volume).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.Volume, EOBSInputTypes.FFMPEGSource));
        input.release();
    });

    it('Set sync offset and get it', () => {
        if (process.platform === 'darwin') {
            return;
        }
        let returnedSyncOffset: ITimeSpec;
        let inputType: string;

        if (obs.os == 'win32') {
            inputType = EOBSInputTypes.WASAPIInput;
        } else if (obs.os == 'darwin') {
            inputType = EOBSInputTypes.CoreAudioInput;
        }

        // Creating input source
        const input = osn.InputFactory.create(inputType, 'input');

        // Checking if input source was created correctly
        expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
        expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
        expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

        // Setting input sync offset
        input.syncOffset = getTimeSpec(5000);

        // Getting input sync offset
        returnedSyncOffset = input.syncOffset;

        // Checking if sync offset value was returned correctly
        expect(returnedSyncOffset).to.eql(getTimeSpec(5000), GetErrorMessage(ETestErrorMsg.SyncOffset, inputType));
        input.release();
    });

    it('Set audio mixers value and get it', () => {
        let returnedAudioMixers: number;
        let inputType: string;

        if (obs.os == 'win32') {
            inputType = EOBSInputTypes.WASAPIOutput;
        } else if (obs.os == 'darwin') {
            inputType = EOBSInputTypes.CoreAudioOutput;
        }

        // Creating input source
        const input = osn.InputFactory.create(inputType, 'input');

        // Checking if input source was created correctly
        expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
        expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
        expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

        // Setting input audio mixers value
        input.audioMixers = 3;

        // Getting input audio mixers value
        returnedAudioMixers = input.audioMixers;

        // Checking if audio mixers value was returned correctly
        expect(returnedAudioMixers).to.equal(3, GetErrorMessage(ETestErrorMsg.AudioMixers, inputType));
        input.release();
    });

    it('Set monitoring type value and get it', () => {
        if (process.platform === 'darwin') {
            return;
        }
        let returnedMonitoringType: osn.EMonitoringType;
        let inputType: string;

        if (obs.os == 'win32') {
            inputType = EOBSInputTypes.WASAPIInput;
        } else if (obs.os == 'darwin') {
            inputType = EOBSInputTypes.CoreAudioInput;
        }

        // Creating input source
        const input = osn.InputFactory.create(inputType, 'input');

        // Checking if input source was created correctly
        expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
        expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
        expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

        // Setting monitoring type value
        input.monitoringType = osn.EMonitoringType.MonitoringAndOutput;

        // Getting monitoring type value
        returnedMonitoringType = input.monitoringType;

        // Checking if monitoring type value was returned correctly
        expect(returnedMonitoringType).to.equal(osn.EMonitoringType.MonitoringAndOutput, GetErrorMessage(ETestErrorMsg.MonitoringType, inputType));
        input.release();
    });

    it('Add video filter to video sources', function() {
        if (obs.isDarwin()) {
            this.skip();
        }
        let videoFilters: string[] = [];
        let addedFilters: string[] = [];

        // Getting all video filter types
        osn.FilterFactory.types().forEach(function(filterType) {
            const video = !!(osn.ESourceOutputFlags.Video & osn.Global.getOutputFlagsFromId(filterType));

            // Filling videoFilters array
            if (video) {
                videoFilters.push(filterType);
            }
        });

        // Creating all video sources available
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            const videoSource = !!(osn.ESourceOutputFlags.Video & osn.Global.getOutputFlagsFromId(inputType));

            if (videoSource) {
                // Creating video source
                const input = osn.InputFactory.create(inputType, inputType);

                // Checking if input source was created correctly
                expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
                expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
                expect(input.name).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputName, inputType));

                videoFilters.forEach(function(filterType) {
                    if (filterType === 'ndi_filter') {
                        if (inputType === 'ndi_source') {
                            const filter = osn.FilterFactory.create(filterType, filterType);

                            // Checking if filter was created correctly
                            expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));

                            // Adding filter to addedFilters array
                            addedFilters.push(filter.name);

                            // Adding ndi filter to ndi source
                            input.addFilter(filter);
                        }
                    } else if (filterType === 'async_delay_filter' ||
                        filterType === 'mediasoupconnector_vfilter') {
                        if (inputType === 'ffmpeg_source' ||
                            inputType === 'dshow_input') {
                            const filter = osn.FilterFactory.create(filterType, filterType);

                            // Checking if filter was created correctly
                            expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));

                            // Adding filter to addedFilters array
                            addedFilters.push(filter.name);

                            // Adding async delay filter to source
                            input.addFilter(filter);
                        }
                    } else {
                        const filter = osn.FilterFactory.create(filterType, filterType);

                        // Checking if filter was created correctly
                        expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));

                        // Adding filter to addedFilters array
                        addedFilters.push(filter.name);

                        // Adding filter to source
                        input.addFilter(filter);
                    }
                });

                // Finding each filter
                addedFilters.forEach(function(filterName) {
                    const foundFilter = input.findFilter(filterName);

                    // Checking if filter was found
                    expect(foundFilter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.FindFilter, filterName, inputType));
                });

                // Cleaning addedFilters array
                addedFilters = [];

                // Removing all filters from input
                input.filters.forEach(function(filter) {
                    input.removeFilter(filter);
                    filter.release();
                });

                // Checking if all filters where removed
                expect(input.filters.length).to.equal(0, GetErrorMessage(ETestErrorMsg.RemoveFilter));

                input.release();
            }
        });
    });

    it('Add async filters to async sources', () => {
        let asyncFilters: string[] = [];
        let addedFilters: string[] = [];

        // Getting all filter types and separating them between video, audio and async
        osn.FilterFactory.types().forEach(function(filterType) {
            const async = !!(osn.ESourceOutputFlags.Async & osn.Global.getOutputFlagsFromId(filterType));

            // Filling asyncFilters array
            if (async) {
                asyncFilters.push(filterType);
            }
        });

        // Create all async sources available
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            const asyncSource = !!(osn.ESourceOutputFlags.Async & osn.Global.getOutputFlagsFromId(inputType));
            const audioSource = !!(osn.ESourceOutputFlags.Audio & osn.Global.getOutputFlagsFromId(inputType));

            if (asyncSource && audioSource) {
                // Creating async source
                const input = osn.InputFactory.create(inputType, inputType);

                // Checking if input source was created correctly
                expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
                expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
                expect(input.name).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputName, inputType));

                asyncFilters.forEach(function(filterType) {
                    const filter = osn.FilterFactory.create(filterType, filterType);

                    // Checking if filter was created correctly
                    expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));

                    // Adding filter to addedFilters array
                    addedFilters.push(filter.name);

                    // Adding filter to source
                    input.addFilter(filter);
                });

                // Finding each filter
                addedFilters.forEach(function(filterName) {
                    const foundFilter = input.findFilter(filterName);

                    // Checking if filter was found
                    expect(foundFilter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.FindFilter, filterName, inputType));
                });

                // Cleaning addedFilters array
                addedFilters = [];

                // Removing all filters from input
                input.filters.forEach(function(filter) {
                    input.removeFilter(filter);
                    filter.release();
                });

                // Checking if all filters where removed
                expect(input.filters.length).to.equal(0, GetErrorMessage(ETestErrorMsg.RemoveFilter));

                input.release();
            }
        });
    });

    it('Add audio filters to audio sources', () => {
        let audioFilters: string[] = [];
        let addedFilters: string[] = [];

        // Getting all audio filter types
        osn.FilterFactory.types().forEach(function(filterType) {
            const audio = !!(osn.ESourceOutputFlags.Audio & osn.Global.getOutputFlagsFromId(filterType));

            // Filling audioFilters array
            if (audio) {
                audioFilters.push(filterType);
            }
        });

        // Creating all audio sources available
        obs.inputTypes.forEach(function(inputType) {
            if(obs.skipSource(inputType)) { return;}
            const audioSource = !!(osn.ESourceOutputFlags.Audio & osn.Global.getOutputFlagsFromId(inputType));

            if (audioSource) {
                // Creating audio source
                const input = osn.InputFactory.create(inputType, 'input');

                // Checking if input source was created correctly
                expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, inputType));
                expect(input.id).to.equal(inputType, GetErrorMessage(ETestErrorMsg.InputId, inputType));
                expect(input.name).to.equal('input', GetErrorMessage(ETestErrorMsg.InputName, inputType));

                audioFilters.forEach(function(filterType) {
                    const filter = osn.FilterFactory.create(filterType, filterType);

                    // Checking if filter was created correctly
                    expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));

                    // Adding filter to addedFilters index
                    addedFilters.push(filter.name);

                    // Adding filter to source
                    input.addFilter(filter);
                });

                // Finding each filter
                addedFilters.forEach(function(filterName) {
                    const foundFilter = input.findFilter(filterName);

                    // Checking if filter was found
                    expect(foundFilter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.FindFilter, filterName, inputType));
                });

                // Cleaning addedFilters array
                addedFilters = [];

                // Removing all filters from input
                input.filters.forEach(function(filter) {
                    input.removeFilter(filter);
                    filter.release();
                });

                // Checking if all filters where removed
                expect(input.filters.length).to.equal(0, GetErrorMessage(ETestErrorMsg.RemoveFilter));

                input.release();
            }
        });
    });

    it('Change the order of filters in the list', () => {
        // Creating source
        const input = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'test_source');

        // Checking if source was created correctly
        expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, EOBSInputTypes.ImageSource));
        expect(input.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.InputId, EOBSInputTypes.ImageSource));
        expect(input.name).to.equal('test_source', GetErrorMessage(ETestErrorMsg.InputName, EOBSInputTypes.ImageSource));

        // Creating filters
        const filter1 = osn.FilterFactory.create(EOBSFilterTypes.Color, 'filter1');
        const filter2 = osn.FilterFactory.create(EOBSFilterTypes.Crop, 'filter2');
        const filter3 = osn.FilterFactory.create(EOBSFilterTypes.GPUDelay, 'filter3');

        // Checking if filters were created correctly
        expect(filter1).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, EOBSFilterTypes.Color));
        expect(filter2).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, EOBSFilterTypes.Crop));
        expect(filter3).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, EOBSFilterTypes.GPUDelay));

        // Adding filters to source
        input.addFilter(filter1);
        input.addFilter(filter2);
        input.addFilter(filter3);

        // Changing filter order down
        input.setFilterOrder(filter1, osn.EOrderMovement.Down);

        // Checking if filter is in the right position
        expect(input.filters[1].name).to.equal('filter1', GetErrorMessage(ETestErrorMsg.MoveFilterDown, EOBSFilterTypes.Color));

        // Changing filter order up
        input.setFilterOrder(filter3, osn.EOrderMovement.Up);

        // Checking if filter is in the right position
        expect(input.filters[1].name).to.equal('filter3', GetErrorMessage(ETestErrorMsg.MoveFilterUp, EOBSFilterTypes.GPUDelay));

        // Changing filter order to bottom
        input.setFilterOrder(filter2, osn.EOrderMovement.Bottom);

        // Checking if filter is in the right position
        expect(input.filters[2].name).to.equal('filter2', GetErrorMessage(ETestErrorMsg.MoveFilterBottom, EOBSFilterTypes.Crop));

        // Changing filter order to top
        input.setFilterOrder(filter2, osn.EOrderMovement.Top);

        // Checking if filter is in the right position
        expect(input.filters[0].name).to.equal('filter2', GetErrorMessage(ETestErrorMsg.MoveFilterTop, EOBSFilterTypes.Crop));

        // Removing all filters
        input.filters.forEach(function(filter) {
            input.removeFilter(filter);
            filter.release();
        });

        input.release();
    });

    it('Fail test - Try to find an input that does not exist', () => {
        let inputFromName: IInput;

        expect(function () {
            inputFromName = osn.InputFactory.fromName('doesNotExist');
        }).to.throw();
    });
});
