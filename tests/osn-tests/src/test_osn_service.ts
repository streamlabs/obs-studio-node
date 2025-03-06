import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles } from '../util/general';
import { EOBSInputTypes } from '../util/obs_enums';

import chai = require('chai');
import chaiSubset = require("chai-subset")
chai.use(chaiSubset);

const testName = 'osn-service';

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

    it('Get service types', () => {
        const services = osn.ServiceFactory.types();
        expect(services).to.eql(['whip_custom', 'rtmp_common', 'rtmp_custom'], 'Wrong services types returned');
    });

    it('Create rtmp common', () => {
        const service = osn.ServiceFactory.create('rtmp_common', 'service', {});
        expect(service).to.not.equal(undefined, 'Error while creating the rtmp comon service');
        expect(service.name).to.equal('service', 'Invalid service name set');

        service.update({
            service: 'Twitch',
            server: 'auto',
            key: 'test'
        });

        // Using containSubset here because starting from OBS 30 the resulting object is bloated with URLs and disclaimers about usage policy
        expect(service.settings).to.containSubset(
            {service: 'Twitch', server: 'auto', key: 'test'});

        const props = service.properties;
        let prop: any = props.first();
        while (prop) {
            prop = prop.next();
            if (prop && prop.name == 'server')
                expect(prop.details.items).to.have.lengthOf.above(1);
        }
        osn.ServiceFactory.destroy(service);
    });

    it('Create rtmp custom', () => {
        const service = osn.ServiceFactory.create('rtmp_custom', 'service', {});
        expect(service).to.not.equal(undefined, 'Error while creating the rtmp comon service');
        expect(service.name).to.equal('service', 'Invalid service name set');

        service.update({
            url: 'url',
            key: 'key',
            username: 'username',
            password: 'password',
        });

        const settings = service.settings;
        expect(settings.url).to.equal('url', 'Error while updating the service url');
        expect(settings.key).to.equal('key', 'Error while updating the service key');
        expect(settings.username).to.equal('username', 'Error while updating the service username');
        expect(settings.password).to.equal('password', 'Error while updating the service password');
        osn.ServiceFactory.destroy(service);
    });
});
