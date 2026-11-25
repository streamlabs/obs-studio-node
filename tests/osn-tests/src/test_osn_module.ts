import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import * as path from 'path';
import * as fs from 'fs';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';

const testName = 'osn-module';

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;
    const pluginFixtures: string[] = [];
    const blockedName = 'unitycapturefilter64bit';
    const userPlugins = path.join(__dirname, '..', 'osnData', 'slobs-plugins');
    const blockedPlugin = path.join(userPlugins, 'obs-plugins', '64bit', blockedName + '.dll');

    // Initialize OBS process
    before(function() {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        if (process.platform === 'win32') {
            const roots = [osn.wd, userPlugins];
            roots.forEach((root, index) => {
                const file = path.join(root, 'obs-plugins', '64bit', `osn-test-invalid-${index}.dll`);
                fs.mkdirSync(path.dirname(file), { recursive: true });
                fs.copyFileSync(path.join(osn.wd, 'libobs-d3d11.dll'), file, fs.constants.COPYFILE_EXCL);
                pluginFixtures.push(file);
            });
            const imageSource = path.join(osn.wd, 'core', 'image-source', 'image-source.dll');
            fs.copyFileSync(imageSource, blockedPlugin, fs.constants.COPYFILE_EXCL);
            pluginFixtures.push(blockedPlugin);
        }
        obs = new OBSHandler(testName);
    });

    // Shutdown OBS process
    after(async function() {
        try {
            if (obs) obs.shutdown();
        } finally {
            pluginFixtures.forEach(file => fs.unlinkSync(file));
        }

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

    it('Loads packaged core modules during startup', () => {
        const requiredModules = ['image-source', 'obs-filters', 'obs-transitions',
            'obs-ffmpeg', 'obs-outputs', 'obs-x264', 'rtmp-services', 'obs-browser'];
        const modules = osn.ModuleFactory.modules();
        const moduleNames = modules.map(file => path.basename(String(file)));
        const failures = osn.NodeObs.OBS_API_getModuleLoadFailures();
        if (process.platform === 'darwin') {
            expect(fs.existsSync(path.join(osn.wd, 'PlugIns', 'frontend-tools.plugin'))).to.equal(false);
            expect(failures.filter(failure => failure.module === 'frontend-tools')).to.deep.equal([]);
        }
        for (const name of requiredModules) {
            const binary = process.platform === 'win32'
                ? path.join(osn.wd, 'core', name, `${name}.dll`)
                : path.join(osn.wd, 'PlugIns', `${name}.plugin`, 'Contents', 'MacOS', name);
            expect(fs.existsSync(binary), binary).to.equal(true);
            expect(moduleNames, `Loaded modules: ${name}`).to.include(path.basename(binary));
            expect(failures.filter(failure => failure.module === name), name).to.deep.equal([]);
        }
        const corePath = path.join(osn.wd, process.platform === 'win32' ? 'core' : 'PlugIns');
        const packagedModules = fs.readdirSync(corePath, { withFileTypes: true })
            .filter(entry => entry.isDirectory() && (process.platform === 'win32' || entry.name.endsWith('.plugin')))
            .map(entry => entry.name.replace(/\.plugin$/, ''));
        for (const name of packagedModules) {
            const fileName = process.platform === 'win32' ? `${name}.dll` : name;
            expect(moduleNames.includes(fileName) || failures.some(failure => failure.module === name),
                `Packaged module ${name} must load or report a diagnostic`).to.equal(true);
        }
        expect(obs.inputTypes).to.include.members(['image_source', 'ffmpeg_source', 'browser_source']);
        expect(osn.VideoEncoderFactory.types()).to.include('obs_x264');
    });

    it('Keeps the explicit module API usable with the core layout', () => {
        const name = 'image-source';
        const binary = process.platform === 'win32'
            ? path.join(osn.wd, 'core', name, `${name}.dll`)
            : path.join(osn.wd, 'PlugIns', `${name}.plugin`, 'Contents', 'MacOS', name);
        const data = process.platform === 'win32'
            ? path.join(osn.wd, 'core', name, 'data')
            : path.join(osn.wd, 'PlugIns', `${name}.plugin`, 'Contents', 'Resources');
        const module = osn.ModuleFactory.open(binary, data);
        expect(() => module.initialize()).not.to.throw();
        expect(path.normalize(module.binaryPath)).to.equal(path.normalize(binary));
        expect(path.normalize(module.dataPath)).to.equal(path.normalize(data));
    });

    it('Blocks a valid plugin with a blocklisted DLL name during and after Windows startup', function () {
        if (process.platform !== 'win32') this.skip();
        const failure = osn.NodeObs.OBS_API_getModuleLoadFailures().find(entry => entry.module === blockedName);
        expect(failure).to.have.property('code', 'MODULE_FAILED_TO_OPEN');
        const data = path.join(osn.wd, 'core', 'image-source', 'data');
        expect(() => osn.ModuleFactory.open(blockedPlugin, data)).to.throw('Failed to create module');
    });

    it('Retains diagnostics from both legacy plugin directories', function () {
        if (process.platform !== 'win32') this.skip();
        const failures = osn.NodeObs.OBS_API_getModuleLoadFailures();
        for (let index = 0; index < 2; index++) {
            expect(failures).to.deep.include({
                module: `osn-test-invalid-${index}`,
                code: 'MODULE_NOT_OBS_PLUGIN',
                message: 'Module is not an OBS plugin',
            });
        }
    });
});
