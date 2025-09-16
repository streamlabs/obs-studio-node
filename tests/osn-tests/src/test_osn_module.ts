import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import * as path from 'path';
import * as fs from 'fs';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';

const testName = 'osn-module';

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

    it('Open all module types and initialize them', () => {
        let moduleTypes: string[] = [];
        let modulePath: string;

        if (obs.os == 'win32') {
            modulePath = path.join(path.normalize(osn.DefaultPluginPath), '64bit');
        } else if (obs.os == 'darwin') {
            modulePath = path.normalize(osn.DefaultPluginPathMac);
        }

        fs.readdirSync(modulePath).forEach(file => {
            if (file.endsWith('.dll')) {
                if (file != 'chrome_elf.dll' && 
                    file != 'libcef.dll' &&
                    file != 'libEGL.dll' &&
                    file != 'libGLESv2.dll' &&
                    file != 'Spout.dll' &&
                    file != 'SpoutDX.dll' &&
                    file != 'SpoutLibrary.dll' &&
                    file != 'onnxruntime.dll' && 
                    file != 'mediasoup-connector.dll') { // Doesn't build in debug mode
                    // Opening module
                    console.log('Opening module: ' + file);
                    const moduleType = osn.ModuleFactory.open(path.join(modulePath, '/' + file), path.normalize(osn.DefaultDataPath));
                    console.log('Opened module: ' + file + ' successfully ' + moduleType);
                    // Checking if module was opened properly
                    expect(moduleType).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.OpenModule, file));

                    // Initializing module
                    expect(function () {
                        moduleType.initialize();
                    }).to.not.throw();

                    // Adding to moduleArrays to use in check later
                    moduleTypes.push(path.join(modulePath, '/' + file));
                }   
            }
        });

        // Getting all modules
        const modules = osn.ModuleFactory.modules();

        // Checking if returned modules are the ones opened
        expect(modules).to.include.members(moduleTypes, GetErrorMessage(ETestErrorMsg.Modules));
    });
});
