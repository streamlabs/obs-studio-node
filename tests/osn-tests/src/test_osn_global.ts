import 'mocha';
import { expect } from 'chai';
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { ISource } from '../osn';
import { OBSHandler } from '../util/obs_handler';
import { deleteConfigFiles } from '../util/general';
import { ETestErrorMsg, GetErrorMessage } from '../util/error_messages';
import { EOBSInputTypes } from '../util/obs_enums';

const testName = 'osn-global';

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
    after(function() {
        obs.shutdown();

        if (hasTestFailed === true) {
            logInfo(testName, 'One or more test cases failed. Uploading cache');
            obs.uploadTestCache();
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

    it('Set source to output channel and get it', function () {
        // Creating input source
        const input = osn.InputFactory.create(EOBSInputTypes.ImageSource, 'test_osn_global_source');

        // Checking if input source was created correctly
        expect(input).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CreateInput, EOBSInputTypes.ImageSource));
        expect(input.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.InputId, EOBSInputTypes.ImageSource));
        expect(input.name).to.equal('test_osn_global_source', GetErrorMessage(ETestErrorMsg.InputName, EOBSInputTypes.ImageSource));

        const channel = 1;
        // Setting input source to output channel
        osn.Global.setOutputSource(channel, input);

        // Getting input source from output channel
        const returnSource = osn.Global.getOutputSource(channel);

        // Checking if input source returned previously is correct
        expect(returnSource).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.NoInputInChannel, channel.toString()));
        expect(returnSource.id).to.equal(EOBSInputTypes.ImageSource, GetErrorMessage(ETestErrorMsg.InputFromChannelId));
        expect(returnSource.name).to.equal('test_osn_global_source', GetErrorMessage(ETestErrorMsg.InputFromChannelName));
        const nullSource : ISource = null;
        osn.Global.setOutputSource(channel, nullSource); // We must clear the channel before deleting the input source to prevent audio thread crash
        input.release();
    });

    it('Get flags (capabilities) of a source type', function ()  {
        let flags: number = undefined;

        // For each input type available get their flags and check if they are not undefined
        obs.inputTypes.forEach(inputType => {
            if(obs.skipSource(inputType)) { return;}
            flags = osn.Global.getOutputFlagsFromId(inputType);
            expect(flags).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.GetOutputFlags, inputType));
            flags = undefined;
        });
    });

    it('Get lagged frames value', function () {
        let laggedFrames: number = undefined;

        // Getting lagged frames value
        laggedFrames = osn.Global.laggedFrames;

        // Checking if lagged frames was returned correctly
        expect(laggedFrames).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.LaggedFrames));
    });

    it('Get total frames value', function () {
        let totalFrames: number = undefined;

        // Getting total frames value
        totalFrames = osn.Global.totalFrames;

        // Checking if total frames was returned correctly
        expect(totalFrames).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.TotalFrames));
    });

    it('Set locale and get it', function () {
        let locale: string;

        // Setting locale
        osn.Global.locale = 'pt-BR';

        // Getting locale
        locale = osn.Global.locale;

        // Checking if locale was returned correctly
        expect(locale).to.equal('pt-BR', GetErrorMessage(ETestErrorMsg.Locale));
    });

    it('Get CPU percentage', function () {
        let cpuPercent: number = undefined;

        // Getting CPU %
        cpuPercent = osn.Global.cpuPercentage;

        // Checking if CPU % was returned correctly
        expect(cpuPercent).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.CPUPercent));
    });

    it('Get current frame rate', function () {
        let frameRate: number = undefined;

        // Getting CPU %
        frameRate = osn.Global.currentFrameRate;

        // Checking if CPU % was returned correctly
        expect(frameRate).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.FrameRate));
    });

    it('Get average time to render', function () {
        let renderTime: number = undefined;

        // Getting CPU %
        renderTime = osn.Global.averageFrameRenderTime;

        // Checking if CPU % was returned correctly
        expect(renderTime).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.FrameRenderTime));
    });

    it('Get available disk space', function () {
        let diskSpace: number = undefined;

        // Getting CPU %
        diskSpace = osn.Global.diskSpaceAvailable;

        // Checking if CPU % was returned correctly
        expect(diskSpace).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.DiskSpace));
    });

    it('Get memory usage', function () {
        let mem: number = undefined;

        // Getting CPU %
        mem = osn.Global.memoryUsage;

        // Checking if CPU % was returned correctly
        expect(mem).to.not.equal(undefined, GetErrorMessage(ETestErrorMsg.MemUsage));
    });

    it('Fail test - Get source from empty output channel', function () {
        let input: ISource;
        let channel: number = 5;

        // Trying to get source from empty channel
        input = osn.Global.getOutputSource(channel);

        // Checking if source is undefined	            
        expect(input).to.equal(undefined, GetErrorMessage(ETestErrorMsg.ChannelNotEmpty, channel.toString()));
    });
});
