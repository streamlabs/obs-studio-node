import 'mocha'
import { expect } from 'chai'
import * as fs from 'fs';
import * as osn from '../osn';
import { logEmptyLine, logInfo } from '../util/logger';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles } from '../util/general';

import path = require('path');

const testName = 'osn-advanced-streaming-rescale-migration';

describe(testName, () => {
    let obs: OBSHandler;

    before(() => {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();

        const configFolderPath = path.join(path.normalize(__dirname), '..', 'osnData/slobs-client');
        fs.mkdirSync(configFolderPath, { recursive: true });
        fs.writeFileSync(
            path.join(configFolderPath, 'basic.ini'),
            '[Output]\nMode=Advanced\n\n[AdvOut]\nRescale=true\nRescaleRes=852x480\n',
        );

        obs = new OBSHandler(testName, false);
    });

    after(() => {
        obs.shutdown();
        obs = null;
        deleteConfigFiles();
        logInfo(testName, 'Finished ' + testName + ' tests');
        logEmptyLine();
    });

    it('Migrates legacy advanced streaming rescale to bilinear filter', () => {
        const stream = osn.AdvancedStreamingFactory.legacySettings;

        expect(stream.rescaling).to.equal(
            true, "Legacy Rescale=true should enable advanced stream rescaling");
        expect((stream as any).rescaleFilter).to.equal(
            osn.EScaleType.Bilinear, "Legacy Rescale=true should migrate to Bilinear filter");
        expect(stream.outputWidth).to.equal(
            852, "Invalid migrated outputWidth value");
        expect(stream.outputHeight).to.equal(
            480, "Invalid migrated outputHeight value");

        osn.AdvancedStreamingFactory.destroy(stream);
    });
});
