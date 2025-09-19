import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles, basicOBSSettingsCategories } from '../util/general';
import { EOBSSettingsCategories } from '../util/obs_enums';

const testName = 'nodeobs_settings';

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

    it('Get and set general settings', function() {
        // Getting general settings
        let generalSettings = obs.getSettingsContainer(EOBSSettingsCategories.General);

        // Changing values of general settings
        generalSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                if (parameter.type === 'OBS_PROPERTY_BOOL') {
                    if (parameter.currentValue === true) {
                        parameter.currentValue = false;
                    } else {
                        parameter.currentValue = true;
                    }
                }

                if (parameter.type === 'OBS_PROPERTY_DOUBLE') {
                    parameter.currentValue = parameter.currentValue + 1;
                }
            });
        });

        // Setting the updated general settings
        obs.setSettingsContainer(EOBSSettingsCategories.General, generalSettings);

        // Checking if general settings were updated correctly
        const updatedGeneralSettings = obs.getSettingsContainer(EOBSSettingsCategories.General);
        expect(generalSettings).to.eql(updatedGeneralSettings, GetErrorMessage(ETestErrorMsg.GeneralSettings));
    });

    it('Get and set stream settings', function() {
        let availableServices: string[] = [];
        const serviceObj = osn.ServiceFactory.legacySettings;

        {
            const props = serviceObj.properties;
            let prop: any = props.first();
            while (prop) {
                if (prop.name === 'service') {
                    for (let item of prop.details.items)
                        availableServices.push(item.name);
                    break;
                }
                prop = prop.next();
            }
        }

        // Changing stream settings of all services available
        availableServices.forEach(service => {
            serviceObj.update({ service: service });
            const settings = serviceObj.settings;

            expect(settings.service).to.equal(service, GetErrorMessage(ETestErrorMsg.SingleStreamSetting, service));
            settings.key = '123test';

            const props = serviceObj.properties;
            let servers = [];
            let prop: any = props.first();
            while (prop) {
                if (prop.name === 'server') {
                    servers = prop.details.items;
                    break;
                }
                prop = prop.next();
            }

            settings.server = servers[0].value;
            serviceObj.update(settings);

            const updatedSettings = serviceObj.settings;

            expect(settings.service).to.equal(updatedSettings.service, GetErrorMessage(ETestErrorMsg.SingleStreamSetting, updatedSettings.service));
            expect(settings.key).to.equal(updatedSettings.key, GetErrorMessage(ETestErrorMsg.SingleStreamSetting, updatedSettings.key));
            expect(settings.server).to.equal(updatedSettings.server, GetErrorMessage(ETestErrorMsg.SingleStreamSetting, updatedSettings.server));
        });
    });

    it('Get and set simple output settings', function() {
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Simple');
        obs.setSetting(EOBSSettingsCategories.Output, 'RecQuality', 'Stream');
        obs.setSetting(EOBSSettingsCategories.Output, 'RecRB', true);

        // Getting simple output settings container with same as stream and replay buffer settings
        let sameAsStreamRBuffOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        sameAsStreamRBuffOutputSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                switch(parameter.name) {
                    case 'Mode': {
                        expect(parameter.currentValue).to.equal('Simple', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    // Streaming
                    case 'VBitrate': {
                        parameter.currentValue = 2000;
                        break;
                    }
                    case 'StreamEncoder': {
                        parameter.currentValue = 'x264';
                        break;
                    }
                    case 'ABitrate': {
                        parameter.currentValue = '320';
                        break;
                    }
                    case 'FileNameWithoutSpace': {
                        parameter.currentValue = true;
                        break;
                    }
                    // Recording
                    case 'RecQuality': {
                        expect(parameter.currentValue).to.equal('Stream', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'RecFormat': {
                        parameter.currentValue = 'mkv';
                        break;
                    }
                    case 'MuxerCustom': {
                        parameter.currentValue = 'test';
                        break;
                    }
                    case 'RecRB': {
                        expect(parameter.currentValue).to.equal(true, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'RecRBTime': {
                        parameter.currentValue = 50;
                        break;
                    }
                }
            });
        });

        // Setting the updated simple output settings
        obs.setSettingsContainer(EOBSSettingsCategories.Output, sameAsStreamRBuffOutputSettings);

        // Checking if output settings were updated correctly
        const updatedSameAsStreamRBuffOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
        expect(sameAsStreamRBuffOutputSettings).to.eql(updatedSameAsStreamRBuffOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

        // Setting recording quality to high
        obs.setSetting(EOBSSettingsCategories.Output, 'RecQuality', 'Small');

        // Getting simple output settings container with high quality settings
        let highQualityOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        highQualityOutputSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {

                switch(parameter.name) {
                    case 'Mode': {
                        expect(parameter.currentValue).to.equal('Simple', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    // Streaming
                    case 'VBitrate': {
                        parameter.currentValue = 3000;
                        break;
                    }
                    case 'StreamEncoder': {
                        expect(parameter.currentValue).to.equal('x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'ABitrate': {
                        parameter.currentValue = '288';
                        break;
                    }
                    case 'FileNameWithoutSpace': {
                        parameter.currentValue = false;
                        break;
                    }
                    // Recording
                    case 'RecQuality': {
                        expect(parameter.currentValue).to.equal('Small', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'RecFormat': {
                        parameter.currentValue = 'mkv';
                        break;
                    }
                    case 'RecEncoder': {
                        parameter.currentValue = 'x264';
                        break;
                    }
                }
            });
        });

        // Setting simple output settings container with high quality settings
        obs.setSettingsContainer(EOBSSettingsCategories.Output, highQualityOutputSettings);

        // Checking if output settings were updated correctly
        const updatedHighQualityOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
        expect(highQualityOutputSettings).to.eql(updatedHighQualityOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

        // Setting recording quality to indistinguishable
        obs.setSetting(EOBSSettingsCategories.Output, 'RecQuality', 'HQ');

        // Getting simple output settings container with indistinguishable recording quality settings
        let indistinguishableQualityOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        indistinguishableQualityOutputSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {

                switch(parameter.name) {
                    case 'Mode': {
                        expect(parameter.currentValue).to.equal('Simple', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    // Streaming
                    case 'VBitrate': {
                        parameter.currentValue = 4000;
                        break;
                    }
                    case 'StreamEncoder': {
                        expect(parameter.currentValue).to.equal('x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'ABitrate': {
                        parameter.currentValue = '256';
                        break;
                    }
                    case 'FileNameWithoutSpace': {
                        parameter.currentValue = true;
                        break;
                    }
                    // Recording
                    case 'RecQuality': {
                        expect(parameter.currentValue).to.equal('HQ', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'RecFormat': {
                        parameter.currentValue ='mkv';
                        break;
                    }
                    case 'RecEncoder': {
                        expect(parameter.currentValue).to.equal('x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                }
            });
        });

        // Getting simple output settings container with indistinguishable recording quality settings
        obs.setSettingsContainer(EOBSSettingsCategories.Output, indistinguishableQualityOutputSettings);

        // Checking if output settings were updated correctly
        const updatedIndistinguishableOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
        expect(indistinguishableQualityOutputSettings).to.eql(updatedIndistinguishableOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

        // Setting recording quality to lossless
        obs.setSetting(EOBSSettingsCategories.Output, 'RecQuality', 'Lossless');

        // Getting simple output settings container with lossless recording quality settings
        let losslessQualityOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        losslessQualityOutputSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {

                switch(parameter.name) {
                    case 'Mode': {
                        expect(parameter.currentValue).to.equal('Simple', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    // Streaming
                    case 'VBitrate': {
                        parameter.currentValue = 5000;
                        break;
                    }
                    case 'StreamEncoder': {
                        expect(parameter.currentValue).to.equal('x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'ABitrate': {
                        parameter.currentValue = '224';
                        break;
                    }
                    case 'FileNameWithoutSpace': {
                        parameter.currentValue = false;
                        break;
                    }
                    // Recording
                    case 'RecQuality': {
                        expect(parameter.currentValue).to.equal('Lossless', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'RecFormat': {
                        parameter.currentValue = 'mkv';
                        break;
                    }
                }
            });
        });

        // Getting simple output settings container with indistinguishable recording quality settings
        obs.setSettingsContainer(EOBSSettingsCategories.Output, losslessQualityOutputSettings);

        // Checking if output settings were updated correctly
        const updatedLosslessOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
        expect(losslessQualityOutputSettings).to.eql(updatedLosslessOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));
    });

    it('Get and set QSV encoder streaming and recording advanced output settings', function() {
        let availableEncoders: string[] = [];

        // Setting output mode to advanced
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Advanced');

        // Getting advanced output settings container
        let settings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        // Getting available encoders
        settings.find(category => {
            return category.nameSubCategory === 'Streaming';
        }).parameters.find(parameter => {
            return parameter.name === 'Encoder';
        }).values.forEach(encoderObject => {
            const encoder = encoderObject[Object.keys(encoderObject)[0]];
            availableEncoders.push(encoder);
        });

        // If QSV encoder is not available, skip this test case
        if (availableEncoders.indexOf('obs_qsv11') < 0) {
            logInfo(testName, 'QSV encoder is not available, skip test case');
            this.skip();
        } else {
            // Setting encoder to QSV
            obs.setSetting(EOBSSettingsCategories.Output, 'Encoder', 'obs_qsv11');
            obs.setSetting(EOBSSettingsCategories.Output, 'RecEncoder', 'obs_qsv11');

            // Setting rate control to CBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'flv');

            // Getting advanced output settings container with CBR parameters
            let cbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'TrackIndex': {
                            parameter.currentValue = '1';
                            break;
                        }
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'ApplyServiceSettings': {
                            parameter.currentValue = false;
                            break;
                        }
                        case 'target_usage': {
                            parameter.currentValue = 'quality';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                        case 'keyint_sec': {
                            parameter.currentValue = 2;
                            break;
                        }
                        case 'async_depth': {
                            parameter.currentValue = 1;
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 3000;
                            break;
                        }
                        // Recording
                        case 'RecFilePath': {
                            parameter.currentValue = 'C:\\Test';
                            break;
                        }
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('flv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecTracks': {
                            expect(parameter.currentValue).to.equal(1);
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecMuxerCustom': {
                            parameter.currentValue = 'test';
                            break;
                        }
                        case 'Rectarget_usage': {
                            parameter.currentValue = 'quality';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                        case 'Reckeyint_sec': {
                            parameter.currentValue = 4;
                            break;
                        }
                        case 'Recasync_depth': {
                            parameter.currentValue = 5;
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 5500;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedCBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cbrOutputSettings).to.eql(updatedCBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to VBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'VBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'VBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mp4');

            // Getting advanced output settings container with VBR parameters
            let vbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            vbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'target_usage': {
                            parameter.currentValue = 'balanced';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 2000;
                            break;
                        }
                        case 'max_bitrate': {
                            parameter.currentValue = 6000;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mp4', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Rectarget_usage': {
                            parameter.currentValue = 'balanced';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 3000;
                            break;
                        }
                        case 'Recmax_bitrate': {
                            parameter.currentValue = 5000;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with VBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, vbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedVBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(vbrOutputSettings).to.eql(updatedVBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to VCM
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'VCM');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'VCM');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mov');

            // Getting advanced output settings container with VCM parameters
            let vcmOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            vcmOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('VCM', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'target_usage': {
                            parameter.currentValue = 'speed';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 1000;
                            break;
                        }
                        case 'max_bitrate': {
                            parameter.currentValue = 4000;
                            break;
                        }
                        //Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mov', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('VCM', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Rectarget_usage': {
                            parameter.currentValue = 'balanced';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 1000;
                            break;
                        }
                        case 'Recmax_bitrate': {
                            parameter.currentValue = 2000;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with VCM parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, vcmOutputSettings);

            // Checking if settings were updated correctly
            const updatedVCMOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(vcmOutputSettings).to.eql(updatedVCMOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to CQP
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CQP');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CQP');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mkv');

            // Getting advanced output settings container with CQP parameters
            let cqpOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cqpOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CQP', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'qpi': {
                            parameter.currentValue = 25;
                            break;
                        }
                        case 'qpp': {
                            parameter.currentValue = 26;
                            break;
                        }
                        case 'qpb': {
                            parameter.currentValue = 27;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mkv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CQP', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recqpi': {
                            parameter.currentValue = 15;
                            break;
                        }
                        case 'Recqpp': {
                            parameter.currentValue = 17;
                            break;
                        }
                        case 'Recqpb': {
                            parameter.currentValue = 13;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CQP parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cqpOutputSettings);

            // Checking if settings were updated correctly
            const updatedCQPOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cqpOutputSettings).to.eql(updatedCQPOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to AVBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'AVBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'AVBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mpegts');

            // Getting advanced output settings container with AVBR parameters
            let avbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            avbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('AVBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 6000;
                            break;
                        }
                        case 'accuracy': {
                            parameter.currentValue = 1000;
                            break;
                        }
                        case 'convergence': {
                            parameter.currentValue = 2;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mpegts', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('AVBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 4500;
                            break;
                        }
                        case 'Recaccuracy': {
                            parameter.currentValue = 1500;
                            break;
                        }
                        case 'Recconvergence': {
                            parameter.currentValue = 3;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with AVBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, avbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedAVBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(avbrOutputSettings).to.eql(updatedAVBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to ICQ
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'ICQ');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'ICQ');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'm3u8');

            // Getting advanced output settings container with ICQ parameters
            let icqOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            icqOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('ICQ', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'icq_quality': {
                            parameter.currentValue = 30;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('m3u8', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('ICQ', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recicq_quality': {
                            parameter.currentValue = 50;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with ICQ parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, icqOutputSettings);

            // Checking if settings were updated correctly
            const updatedICQOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(icqOutputSettings).to.eql(updatedICQOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to LA_ICQ
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'LA_ICQ');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'LA_ICQ');

            // Getting advanced output settings container with LA_ICQ parameters
            let la_icqOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            la_icqOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('LA_ICQ', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'icq_quality': {
                            parameter.currentValue = 10;
                            break;
                        }
                        case 'la_depth': {
                            parameter.currentValue = 50;
                            break;
                        }
                        // Recording
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'recrate_control': {
                            expect(parameter.currentValue).to.equal('LA_ICQ', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recicq_quality': {
                            parameter.currentValue = 200;
                            break;
                        }
                        case 'Recla_depth': {
                            parameter.currentValue = 35;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with LA_ICQ parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, la_icqOutputSettings);

            // Checking if settings were updated correctly
            const updatedLA_ICQOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(la_icqOutputSettings).to.eql(updatedLA_ICQOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to LA
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'LA');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'LA');

            // Setting rescale to true
            obs.setSetting(EOBSSettingsCategories.Output, 'Rescale', true);
            obs.setSetting(EOBSSettingsCategories.Output, 'RecRescale', true);

            // Getting advanced output settings container with LA parameters
            let laOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            laOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Rescale': {
                            expect(parameter.currentValue).to.equal(true, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RescaleRes': {
                            parameter.currentValue = '1920x1080';
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('LA', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 5500;
                            break;
                        }
                        case 'la_depth': {
                            parameter.currentValue = 40;
                            break;
                        }
                        // Recording
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_qsv11', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecRescale': {
                            expect(parameter.currentValue).to.equal(true, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecRescaleRes': {
                            parameter.currentValue = '1920x1080';
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('LA', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 3000;
                            break;
                        }
                        case 'Recla_depth': {
                            parameter.currentValue = 20;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with LA parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, laOutputSettings);

            // Checking if LA advanced settings were updated correctly
            const updatedLAOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(laOutputSettings).to.eql(updatedLAOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));
        }
    });

    it('Get and set x264 encoder streaming and recording advanced output settings', function() {
        let availableEncoders: string[] = [];

        // Setting output mode to advanced
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Advanced');

        // Getting advanced output settings container
        let settings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        // Getting available encoders
        settings.find(category => {
            return category.nameSubCategory === 'Streaming';
        }).parameters.find(parameter => {
            return parameter.name === 'Encoder';
        }).values.forEach(encoderObject => {
            const encoder = encoderObject[Object.keys(encoderObject)[0]];
            availableEncoders.push(encoder);
        });

        // Checking if x264 encoder is available
        if (availableEncoders.indexOf('obs_x264') < 0) {
            logInfo(testName, 'x264 encoder is not available, skip test case');
            this.skip();
        } else {
            // Setting encoder to x264
            obs.setSetting(EOBSSettingsCategories.Output, 'Encoder', 'obs_x264');
            obs.setSetting(EOBSSettingsCategories.Output, 'RecEncoder', 'obs_x264');

            // Setting rate control to CBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'flv');

            // Getting advanced output settings container with CBR parameters
            let cbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'TrackIndex': {
                            parameter.currentValue = '5';
                            break;
                        }
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'ApplyServiceSettings': {
                            parameter.currentValue = true;
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 3000;
                            break;
                        }
                        case 'keyint_sec': {
                            parameter.currentValue = 5;
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'ultrafast';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        case 'tune': {
                            parameter.currentValue = 'film';
                            break;
                        }
                        case 'x264opts': {
                            parameter.currentValue = 'test';
                            break;
                        }
                        // Recording
                        case 'RecFilePath': {
                            parameter.currentValue = 'C:\\Test\\AnotherTest';
                            break;
                        }
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('flv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecTracks': {
                            expect(parameter.currentValue).to.equal(1);
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecMuxerCustom': {
                            parameter.currentValue = 'anotherTest';
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 4100;
                            break;
                        }
                        case 'Reckeyint_sec': {
                            parameter.currentValue = 2;
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = 'placebo';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                        case 'Rectune': {
                            parameter.currentValue = 'zerolatency';
                            break;
                        }
                        case 'Recx264opts': {
                            parameter.currentValue = 'anotherTest';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cbrOutputSettings);

            // Checking settings were updated correctly
            const updatedCBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cbrOutputSettings).to.eql(updatedCBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to ABR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'ABR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'ABR');

            // Setting use buff size
            obs.setSetting(EOBSSettingsCategories.Output, 'use_bufsize', true);
            obs.setSetting(EOBSSettingsCategories.Output, 'Recuse_bufsize', true);

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mp4');

            // Getting advanced output settings container with ABR parameters
            let abrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            abrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('ABR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'superfast';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        case 'tune': {
                            parameter.currentValue = 'animation';
                            break;
                        }
                        case 'use_bufsize': {
                            expect(parameter.currentValue).to.equal(true, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'buffer_size': {
                            parameter.currentValue = 3500;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mp4', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('ABR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = 'veryslow';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        case 'Rectune': {
                            parameter.currentValue = 'fastdecode';
                            break;
                        }
                        case 'Recuse_bufsize': {
                            expect(parameter.currentValue).to.equal(true, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recbuffer_size': {
                            parameter.currentValue = 1500;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with ABR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, abrOutputSettings);

            // Checking if settings were updated correctly
            const updatedABROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(abrOutputSettings).to.eql(updatedABROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to VBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'VBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'VBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mov');

            // Getting advanced output settings container with VBR parameters
            let vbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            vbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'veryfast';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                        case 'tune': {
                            parameter.currentValue = 'grain';
                            break;
                        }
                        case 'crf': {
                            parameter.currentValue = 26;
                            break;
                        }
                        case 'keyint_sec': {
                            parameter.currentValue = 2;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mov', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = 'slower';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        case 'Rectune': {
                            parameter.currentValue = 'ssim';
                            break;
                        }
                        case 'Reccrf': {
                            parameter.currentValue = 18;
                            break;
                        }
                        case 'Reckeyint_sec': {
                            parameter.currentValue = 7;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with VBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, vbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedVBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(vbrOutputSettings).to.eql(updatedVBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to CRF
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CRF');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CRF');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mkv');

            // Getting advanced output settings container with CRF parameters
            let crfOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            crfOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CRF', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'faster';
                            break;
                        }
                        case 'tune': {
                            parameter.currentValue = 'stillimage';
                            break;
                        }
                        case 'crf': {
                            parameter.currentValue = 8;
                            break;
                        }
                        case 'keyint_sec': {
                            parameter.currentValue = 6;
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mkv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_x264', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CRF', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = 'slow';
                            break;
                        }
                        case 'Rectune': {
                            parameter.currentValue = 'psnr';
                            break;
                        }
                        case 'Reccrf': {
                            parameter.currentValue = 11;
                            break;
                        }
                        case 'Reckeyint_sec': {
                            parameter.currentValue = 2;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CRF parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, crfOutputSettings);

            // Checking if settings were updated correctly
            const updatedCRFOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(crfOutputSettings).to.eql(updatedCRFOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));
        }
    });

    it('Get and set NVENC encoder streaming and recording advanced output settings', function() {
        let availableEncoders: string[] = [];

        // Setting output mode to advanced
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Advanced');

        // Getting advanced output settings container
        let settings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        // Getting available encoders
        settings.find(category => {
            return category.nameSubCategory === 'Streaming';
        }).parameters.find(parameter => {
            return parameter.name === 'Encoder';
        }).values.forEach(encoderObject => {
            const encoder = encoderObject[Object.keys(encoderObject)[0]];
            availableEncoders.push(encoder);
        });

        // Checking if NVENC encoder is available
        if (availableEncoders.indexOf('ffmpeg_nvenc') < 0) {
            logInfo(testName, 'NVENC encoder is not available, skip test case');
            this.skip();
        } else {
            // Setting encoder to NVENC
            obs.setSetting(EOBSSettingsCategories.Output, 'Encoder', 'ffmpeg_nvenc');
            obs.setSetting(EOBSSettingsCategories.Output, 'RecEncoder', 'ffmpeg_nvenc');

            // Setting rate control to CBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'flv');

            // Getting advanced output settings container with CBR parameters
            let cbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'TrackIndex': {
                            parameter.currentValue = '4';
                            break;
                        }
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'ApplyServiceSettings': {
                            parameter.currentValue = false;
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 3000;
                            break;
                        }
                        case 'keyint_sec': {
                            parameter.currentValue = 5;
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'mq';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                        case 'gpu': {
                            parameter.currentValue = 1;
                            break;
                        }
                        case 'bf': {
                            parameter.currentValue = 3;
                            break;
                        }
                        // Recording
                        case 'RecFilePath': {
                            parameter.currentValue = 'C:\\Test\\AnotherTest\\MoreTest';
                            break;
                        }
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('flv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecTracks': {
                            expect(parameter.currentValue).to.equal(1, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecMuxerCustom': {
                            parameter.currentValue = 'moreTest';
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 3200;
                            break;
                        }
                        case 'Reckeyint_sec': {
                            parameter.currentValue = 9;
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = '11hp';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        case 'Recgpu': {
                            parameter.currentValue = 2;
                            break;
                        }
                        case 'Recbf': {
                            parameter.currentValue = 5;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedCBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cbrOutputSettings).to.eql(updatedCBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to VBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'VBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'VBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mp4');

            // Getting advanced output settings container with VBR parameters
            let vbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            vbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'hq';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mp4', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = '11hq';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with VBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, vbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedVBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(vbrOutputSettings).to.eql(updatedVBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to CQP
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CQP');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CQP');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mov');

            // Getting advanced output settings container with CQP parameters
            let cqpOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cqpOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CQP', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'cqp': {
                            parameter.currentValue = 25;
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'default';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mov', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CQP', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Reccqp': {
                            parameter.currentValue = 31;
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = '11';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CQP parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cqpOutputSettings);

            // Checking if settings were updated correctly
            const updatedCQPOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cqpOutputSettings).to.eql(updatedCQPOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to Lossless
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'lossless');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'lossless');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mkv');

            // Getting advanced output settings container with Lossless parameters
            let losslessOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            losslessOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('lossless', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'hp';
                            break;
                        }
                        // Recording
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('mkv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('ffmpeg_nvenc', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('lossless', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = 'hp';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with Lossless parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, losslessOutputSettings);

            // Checking if Lossless advanced settings were updated correctly
            const updatedLosslessOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(losslessOutputSettings).to.eql(updatedLosslessOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));
        }
    });

    it('Get and set New NVENC encoder streaming and recording advanced output settings', function() {
        let availableEncoders: string[] = [];

        // Setting output mode to advanced
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Advanced');

        // Getting advanced output settings container
        let settings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        // Getting available encoders
        settings.find(category => {
            return category.nameSubCategory === 'Streaming';
        }).parameters.find(parameter => {
            return parameter.name === 'Encoder';
        }).values.forEach(encoderObject => {
            const encoder = encoderObject[Object.keys(encoderObject)[0]];
            availableEncoders.push(encoder);
        });

        // Checking if NVENC encoder is available
        if (availableEncoders.indexOf('ffmpeg_nvenc') < 0) {
            logInfo(testName, 'New NVENC encoder is not available, skip test case');
            this.skip();
        } else {
            // Setting encoder to new NVENC
            obs.setSetting(EOBSSettingsCategories.Output, 'Encoder', 'obs_nvenc_h264_tex');
            obs.setSetting(EOBSSettingsCategories.Output, 'RecEncoder', 'obs_nvenc_h264_tex');

            // Setting rate control to CBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'flv');

            // Getting advanced output settings container with CBR parameters
            let cbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'TrackIndex': {
                            parameter.currentValue = '2';
                            break;
                        }
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'ApplyServiceSettings': {
                            parameter.currentValue = false;
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'bitrate': {
                            parameter.currentValue = 5200;
                            break;
                        }
                        case 'keyint_sec': {
                            parameter.currentValue = 1
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'mq';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                        case 'lookahead': {
                            parameter.currentValue = false;
                            break;
                        }
                        case 'psycho_aq': {
                            parameter.currentValue = true;
                            break;
                        }
                        case 'gpu': {
                            parameter.currentValue = 1;
                            break;
                        }
                        case 'bf': {
                            parameter.currentValue = 5;
                            break;
                        }
                        // Recording
                        case 'RecFilePath': {
                            parameter.currentValue = 'C:\\Test\\AnotherTest\\MoreTest';
                            break;
                        }
                        case 'RecFormat': {
                            expect(parameter.currentValue).to.equal('flv', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecTracks': {
                            expect(parameter.currentValue).to.equal(1, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'RecMuxerCustom': {
                            parameter.currentValue = 'moreTest';
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CBR');
                            break;
                        }
                        case 'Recbitrate': {
                            parameter.currentValue = 5400;
                            break;
                        }
                        case 'Reckeyint_sec': {
                            parameter.currentValue = 2;
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = '11hp';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        case 'Reclookahead': {
                            parameter.currentValue = false;
                            break;
                        }
                        case 'Recpsycho_aq': {
                            parameter.currentValue = true;
                            break;
                        }
                        case 'Recgpu': {
                            parameter.currentValue = 1;
                            break;
                        }
                        case 'Recbf': {
                            parameter.currentValue = 2;
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedCBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cbrOutputSettings).to.eql(updatedCBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to VBR
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'VBR');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'VBR');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mp4');

            // Getting advanced output settings container with VBR parameters
            let vbrOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            vbrOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'hq';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                        // Recording
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('VBR', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = '11hq';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'main';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with VBR parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, vbrOutputSettings);

            // Checking if settings were updated correctly
            const updatedVBROutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(vbrOutputSettings).to.eql(updatedVBROutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to CQP
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'CQP');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'CQP');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mov');

            // Getting advanced output settings container with CQP parameters
            let cqpOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            cqpOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('CQP', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'default';
                            break;
                        }
                        case 'profile': {
                            parameter.currentValue = 'baseline';
                            break;
                        }
                        case 'cqp': {
                            parameter.currentValue = 35;
                            break;
                        }
                        // Recording
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('CQP', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Reccqp': {
                            parameter.currentValue = 40;
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = '11';
                            break;
                        }
                        case 'Recprofile': {
                            parameter.currentValue = 'high';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with CQP parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, cqpOutputSettings);

            // Checking if settings were updated correctly
            const updatedCQPOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(cqpOutputSettings).to.eql(updatedCQPOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));

            // Setting rate control to Lossless
            obs.setSetting(EOBSSettingsCategories.Output, 'rate_control', 'lossless');
            obs.setSetting(EOBSSettingsCategories.Output, 'Recrate_control', 'lossless');

            // Setting recording format
            obs.setSetting(EOBSSettingsCategories.Output, 'RecFormat', 'mkv');

            // Getting advanced output settings container with Lossless parameters
            let losslessOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

            losslessOutputSettings.forEach(subCategory => {
                subCategory.parameters.forEach(parameter => {
                    switch(parameter.name) {
                        case 'Mode': {
                            expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        // Streaming
                        case 'Encoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'rate_control': {
                            expect(parameter.currentValue).to.equal('lossless', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'preset': {
                            parameter.currentValue = 'hp';
                            break;
                        }
                        // Recording
                        case 'RecEncoder': {
                            expect(parameter.currentValue).to.equal('obs_nvenc_h264_tex', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recrate_control': {
                            expect(parameter.currentValue).to.equal('lossless', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                            break;
                        }
                        case 'Recpreset': {
                            parameter.currentValue = 'hp';
                            break;
                        }
                    }
                });
            });

            // Setting advanced output settings container with Lossless parameters
            obs.setSettingsContainer(EOBSSettingsCategories.Output, losslessOutputSettings);

            // Checking if Lossless advanced settings were updated correctly
            const updatedLosslessOutputSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
            expect(losslessOutputSettings).to.eql(updatedLosslessOutputSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));
        }
    });

    it('Get and set audio tracks and replay buffer advanced output settings', function() {
        // Setting output mode to advanced
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Advanced');
        obs.setSetting(EOBSSettingsCategories.Output, 'RecRB', true);

        // Getting advanced output settings
        let audioTrackReplayBufferSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);

        audioTrackReplayBufferSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                switch(parameter.name) {
                    case 'Mode': {
                        expect(parameter.currentValue).to.equal('Advanced', GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'Track1Bitrate': {
                        parameter.currentValue = '64';
                        break;
                    }
                    case 'Track1Name': {
                        parameter.currentValue = 'Test1';
                        break;
                    }
                    case 'Track2Bitrate': {
                        parameter.currentValue = '96';
                        break;
                    }
                    case 'Track2Name': {
                        parameter.currentValue = 'Test2';
                        break;
                    }
                    case 'Track3Bitrate': {
                        parameter.currentValue = '128';
                        break;
                    }
                    case 'Track3Name': {
                        parameter.currentValue = 'Test3';
                        break;
                    }
                    case 'Track4Bitrate': {
                        parameter.currentValue = '160';
                        break;
                    }
                    case 'Track4Name': {
                        parameter.currentValue = 'Test4';
                        break;
                    }
                    case 'Track5Bitrate': {
                        parameter.currentValue = '192';
                        break;
                    }
                    case 'Track5Name': {
                        parameter.currentValue = 'Test5';
                        break;
                    }
                    case 'Track6Bitrate': {
                        parameter.currentValue = '224';
                        break;
                    }
                    case 'Track6Name': {
                        parameter.currentValue = 'Test6';
                        break;
                    }
                    case 'RecRB': {
                        expect(parameter.currentValue).to.equal(true, GetErrorMessage(ETestErrorMsg.SingleOutputSetting, parameter.name));
                        break;
                    }
                    case 'RecRBTime': {
                        parameter.currentValue = 60;
                        break;
                    }
                }
            });
        });

        // Setting advanced output settings
        obs.setSettingsContainer(EOBSSettingsCategories.Output, audioTrackReplayBufferSettings);

        // Checking if settings were updated correctly
        const updatedSettings = obs.getSettingsContainer(EOBSSettingsCategories.Output);
        expect(audioTrackReplayBufferSettings).to.eql(updatedSettings, GetErrorMessage(ETestErrorMsg.OutputSettings));
    });

    it('Get and set video settings', function() {
        // Setting base resolution to 1920x1080 and FPS type to common
        obs.setSetting(EOBSSettingsCategories.Video, 'Base', '1920x1080');
        obs.setSetting(EOBSSettingsCategories.Video, 'FPSType', 'Common FPS Values');

        // Getting video settings container with common fps settings
        let commonFPSVideoSettings = obs.getSettingsContainer(EOBSSettingsCategories.Video);

        commonFPSVideoSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                switch(parameter.name) {
                    case 'Base': {
                        expect(parameter.currentValue).to.equal('1920x1080', GetErrorMessage(ETestErrorMsg.SingleVideoSetting, parameter.name));
                        break;
                    }
                    case 'Output': {
                        parameter.currentValue = '640x360';
                        break;
                    }
                    case 'ScaleType': {
                        parameter.currentValue = 'bilinear';
                        break;
                    }
                    case 'FPSType': {
                        expect(parameter.currentValue).to.equal('Common FPS Values', GetErrorMessage(ETestErrorMsg.SingleVideoSetting, parameter.name));
                        break;
                    }
                    case 'FPSCommon': {
                        parameter.currentValue = '10';
                        break;
                    }
                }
            });
        });

        // Setting video settings container with common FPS settings
        obs.setSettingsContainer(EOBSSettingsCategories.Video, commonFPSVideoSettings);

        // Checking if settings were updated correctly
        const updatedCommonFPSVideoSettings = obs.getSettingsContainer(EOBSSettingsCategories.Video);
        expect(commonFPSVideoSettings).to.eql(updatedCommonFPSVideoSettings, GetErrorMessage(ETestErrorMsg.VideoSettings));

        // Setting base resolution to 1280x720 and FPS type to integer
        obs.setSetting(EOBSSettingsCategories.Video, 'Base', '1280x720');
        obs.setSetting(EOBSSettingsCategories.Video, 'FPSType', 'Integer FPS Value');

        // Getting video settings container with integer FPS settings
        let integerFPSVideoSettings = obs.getSettingsContainer(EOBSSettingsCategories.Video);

        integerFPSVideoSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                switch(parameter.name) {
                    case 'Base': {
                        expect(parameter.currentValue).to.equal('1280x720', GetErrorMessage(ETestErrorMsg.SingleVideoSetting, parameter.name));
                        break;
                    }
                    case 'Output': {
                        parameter.currentValue = '730x410';
                        break;
                    }
                    case 'ScaleType': {
                        parameter.currentValue = 'bicubic';
                        break;
                    }
                    case 'FPSType': {
                        expect(parameter.currentValue).to.equal('Integer FPS Value', GetErrorMessage(ETestErrorMsg.SingleVideoSetting, parameter.name));
                        break;
                    }
                    case 'FPSInt': {
                        parameter.currentValue = 60;
                        break;
                    }
                }
            });
        });

        // Setting video settings container with integer FPS settings
        obs.setSettingsContainer(EOBSSettingsCategories.Video, integerFPSVideoSettings);

        // Checking if settings were updated correctly
        const updatedIntegerFPSVideoSettings = osn.NodeObs.OBS_settings_getSettings('Video').data;
        expect(integerFPSVideoSettings).to.eql(updatedIntegerFPSVideoSettings, GetErrorMessage(ETestErrorMsg.VideoSettings));

        // Setting FPS type to fractional
        obs.setSetting(EOBSSettingsCategories.Video, 'FPSType', 'Fractional FPS Value');

        // Getting video settings container with fractional FPS settings
        let fractionalFPSVideoSettings = obs.getSettingsContainer(EOBSSettingsCategories.Video);

        fractionalFPSVideoSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                switch(parameter.name) {
                    case 'Output': {
                        parameter.currentValue = '960x540';
                        break;
                    }
                    case 'ScaleType': {
                        parameter.currentValue = 'lanczos';
                        break;
                    }
                    case 'FPSType': {
                        expect(parameter.currentValue).to.equal('Fractional FPS Value', GetErrorMessage(ETestErrorMsg.SingleVideoSetting, parameter.name));
                        break;
                    }
                    case 'FPSNum': {
                        parameter.currentValue = 90;
                        break;
                    }
                    case 'FPSDen': {
                        parameter.currentValue = 5;
                        break;
                    }
                }
            });
        });

        // Setting video settings container with integer FPS settings
        obs.setSettingsContainer(EOBSSettingsCategories.Video, fractionalFPSVideoSettings);

        // Checking if settings were updated correctly
        const updatedFractionalFPSVideoSettings = obs.getSettingsContainer(EOBSSettingsCategories.Video);
        expect(fractionalFPSVideoSettings).to.eql(updatedFractionalFPSVideoSettings, GetErrorMessage(ETestErrorMsg.VideoSettings));
    });

    it('Get and set advanced settings', function() {
        // Getting advanced settings container
        let advancedSettings = obs.getSettingsContainer(EOBSSettingsCategories.Advanced);

        // Changing advanced settings values
        advancedSettings.forEach(subCategory => {
            subCategory.parameters.forEach(parameter => {
                switch(parameter.name) {
                    case 'ProcessPriority': {
                        parameter.currentValue = 'AboveNormal';
                        break;
                    }
                    case 'ColorFormat': {
                        parameter.currentValue = 'NV12';
                        break;
                    }
                    case 'ColorSpace': {
                        parameter.currentValue = '601';
                        break;
                    }
                    case 'ForceGPUAsRenderDevice': {
                        parameter.currentValue = false;
                        break;
                    }
                    case 'DisableAudioDucking': {
                        parameter.currentValue = true;
                        break;
                    }
                    case 'FilenameFormatting': {
                        parameter.currentValue = '%CCYY-%MM-%DD';
                        break;
                    }
                    case 'OverwriteIfExists': {
                        parameter.currentValue = true;
                        break;
                    }
                    case 'RecRBPrefix': {
                        parameter.currentValue = 'TestPrefix';
                        break;
                    }
                    case 'RecRBSuffix': {
                        parameter.currentValue = 'TestSuffix';
                        break;
                    }
                    case 'DelayEnable': {
                        parameter.currentValue = true;
                        break;
                    }
                    case 'DelaySec': {
                        parameter.currentValue = 60;
                        break;
                    }
                    case 'DelayPreserve': {
                        parameter.currentValue = false;
                        break;
                    }
                    case 'Reconnect': {
                        parameter.currentValue = false;
                        break;
                    }
                    case 'RetryDelay': {
                        parameter.currentValue = 15;
                        break;
                    }
                    case 'MaxRetries': {
                        parameter.currentValue = 5;
                        break;
                    }
                    case 'NewSocketLoopEnable': {
                        parameter.currentValue = true;
                        break;
                    }
                    case 'LowLatencyEnable': {
                        parameter.currentValue = true;
                        break;
                    }
                    case 'BrowserHWAccel': {
                        parameter.currentValue = false;
                        break;
                    }
                }
            });
        });

        // Setting advanced settings
        obs.setSettingsContainer(EOBSSettingsCategories.Advanced, advancedSettings);

        // Checking if advanced settings were updated correctly
        const updatedAdvancedSettings = obs.getSettingsContainer(EOBSSettingsCategories.Advanced);
        expect(advancedSettings).to.eql(updatedAdvancedSettings, GetErrorMessage(ETestErrorMsg.AdvancedSettings));
    });

    it('Get all settings categories', function() {
        // Getting categories list
        const categories = osn.NodeObs.OBS_settings_getListCategories();

        // Checking if categories list contains the basic settings categories
        expect(categories.length).to.not.equal(0, GetErrorMessage(ETestErrorMsg.EmptyCategoriesList));
        expect(categories).to.include.members(basicOBSSettingsCategories, GetErrorMessage(ETestErrorMsg.CategoriesListIsMissingValue));
    });
});
