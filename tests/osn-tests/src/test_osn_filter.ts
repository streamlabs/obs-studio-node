import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ISettings } from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import * as filterSettings from '../util/filter_settings';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';

const testName = 'osn-filter';

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

    it('Create all filter types', () => {
        // Create each filter type available
        obs.filterTypes.forEach(function(filterType) {
            const filter = osn.FilterFactory.create(filterType, 'filter');

            // Checking if filter was created correctly
            expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));
            expect(filter.id).to.equal(filterType, GetErrorMessage(ETestErrorMsg.FilterId, filterType));
            expect(filter.name).to.equal('filter', GetErrorMessage(ETestErrorMsg.FilterName, filterType));
            filter.release();
        });
    });

    it('Create all filter types with settings', () => {
        // Create each filter type availabe passing settings parameter
        obs.filterTypes.forEach(function(filterType) {
            let settings: ISettings = {};

            switch(filterType) {
                case 'mask_filter_v2':
                case 'mask_filter': {
                    settings = filterSettings.mask;
                    settings['opacity'] = 80;
                    break; 
                }
                case 'crop_filter': {
                    settings = filterSettings.crop;
                    settings['relative'] = false;
                    break;
                }
                case 'gain_filter': {
                    settings = filterSettings.gain;
                    settings['db'] = 10;
                    break;
                }
                case 'color_filter_v2': {
                    settings = filterSettings.color;
                    settings['brightness'] = 50;
                    settings['color_multiply'] = 16777215;
                    settings['color_add'] = 0;
                    break;
                }
                case 'color_filter': {
                    settings = filterSettings.color;
                    settings['brightness'] = 50;
                    break;
                }
                case 'scale_filter': {
                    settings = filterSettings.scale;
                    settings['undistort'] = true;
                    break;
                }
                case 'scroll_filter': {
                    settings = filterSettings.scroll;
                    settings['cx'] = 200;
                    settings['loop'] = true;
                    break;
                }
                case 'color_key_filter_v2':
                case 'color_key_filter': {
                    settings = filterSettings.colorKey;
                    settings['smoothness'] = 80;
                    break;
                }
                case 'clut_filter': {
                    settings = filterSettings.clut;
                    settings['clut_amount'] = 2;
                    settings['passthrough_alpha'] = false;
                    break;
                }
                case 'sharpness_filter_v2':
                case 'sharpness_filter': {
                    settings = filterSettings.sharpness;
                    settings['sharpness'] = 0.15;
                    break;
                }
                case 'chroma_key_filter_v2':
                case 'chroma_key_filter': {
                    settings = filterSettings.chromaKey;
                    settings['spill'] = 75;
                    break;
                }
                case 'noise_suppress_filter': {
                    settings = filterSettings.noiseSuppress;
                    settings['method'] = "speex";
                    settings['suppress_level'] = -20;
                    settings['intensity'] = 1;
                    break;
                }
                case 'noise_suppress_filter_v2': {
                    settings = filterSettings.noiseSuppress;
                    settings['method'] = "rnnoise";
                    settings['suppress_level'] = -30;
                    settings['intensity'] = 1;
                    break;
                }
                case 'noise_gate_filter': {
                    settings = filterSettings.noiseGate;
                    settings['hold_time'] = 300;
                    break; 
                }
                case 'compressor_filter': {
                    settings = filterSettings.compressor;
                    settings['ratio'] = 5;
                    break;
                }
                case 'limiter_filter': {
                    settings = filterSettings.limiter;
                    settings['threshold'] = -5;
                    break;
                }
                case 'expander_filter': {
                    settings = filterSettings.expander;
                    settings['attack_time'] = 20;
                    break;
                }
                case 'luma_key_filter_v2':
                case 'luma_key_filter': {
                    settings = filterSettings.lumaKey;
                    settings['luma_max'] = 2;
                    break;
                }
                case 'ndi_filter': 
                case 'ndi_audiofilter': {
                    settings = filterSettings.ndi;
                    settings['ndi_filter_ndiname'] = 'Test Output';
                    break;
                }
                case 'hdr_tonemap_filter': {
                    settings = filterSettings.hdrTonemap;
                    settings['sdr_white_level_nits'] = 400;
                    break;
                }
                case 'basic_eq_filter': {
                    settings = filterSettings.basicEq;
                    break;
                }
                case 'upward_compressor_filter': {
                    settings = filterSettings.upwardCompressor;
                    break;
                }
            }

            const filter = osn.FilterFactory.create(filterType, 'filter', settings);

            // Checking if filter was created correctly
            expect(filter).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateFilter, filterType));
            expect(filter.id).to.equal(filterType, GetErrorMessage(ETestErrorMsg.FilterId, filterType));
            expect(filter.name).to.equal('filter', GetErrorMessage(ETestErrorMsg.FilterName, filterType));
            expect(filter.settings).to.eql(settings, ETestErrorMsg.FilterSetting);
            filter.release();
        });
    });

    it('Verify obsolete filters are not returned to the caller', () => {
        // These v1 filters are superseded by v2 equivalents and marked OBS_SOURCE_CAP_OBSOLETE.
        const knownObsoleteFilters = [
            'color_filter',
            'color_key_filter',
            'chroma_key_filter',
            'sharpness_filter',
            'mask_filter',
            'luma_key_filter',
            'noise_suppress_filter',
        ];

        knownObsoleteFilters.forEach(function(id) {
            expect(obs.filterTypes).to.not.include(id, `Obsolete filter '${id}' should not be returned to the caller`);
        });
    });
});
