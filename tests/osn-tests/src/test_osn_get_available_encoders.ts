import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles } from '../util/general';
import { ERecordingFormat } from '../osn';

import path = require('path');

const testName = 'osn-get-available-encoders';
const av1EncoderNames = new Set([
    'ffmpeg_aom_av1',
    'ffmpeg_svt_av1',
    'obs_nvenc_av1_tex',
    'obs_qsv11_av1',
    'av1_texture_amf',
]);

function getEncoderNames(encoders: { name: string }[]): string[] {
    return encoders.map(encoder => encoder.name);
}

function getAv1EncoderNames(encoders: { name: string }[]): string[] {
    return getEncoderNames(encoders).filter(name => av1EncoderNames.has(name));
}

describe(testName, () => {
    let obs: OBSHandler;
    let hasTestFailed: boolean = false;

    // Initialize OBS process
    before(async() => {
        logInfo(testName, 'Starting ' + testName + ' tests');
        deleteConfigFiles();
        obs = new OBSHandler(testName);

        obs.instantiateUserPool(testName);

        // Reserving user from pool
        await obs.reserveUser();
    });

    // Shutdown OBS process
    after(async function() {
        // Releasing user got from pool
        await obs.releaseUser();

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

    it('Get available encoders for simple streaming', async () => {
        const stream = osn.SimpleStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the simple streaming output");

        stream.service = osn.ServiceFactory.legacySettings;

        const encoders = stream.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");
        expect(encoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder");

        for (const encoder of encoders) {
            expect(encoder).to.have.property('title');
            expect(encoder).to.have.property('name');
            expect(encoder.title).to.be.a('string',
                "Encoder title should be a string");
            expect(encoder.name).to.be.a('string',
                "Encoder name should be a string");
            expect(encoder.title).to.not.equal('',
                "Encoder title should not be empty");
            expect(encoder.name).to.not.equal('',
                "Encoder name should not be empty");
        }

        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Get available encoders for simple streaming without service', async () => {
        const stream = osn.SimpleStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the simple streaming output");

        const encoders = stream.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");

        for (const encoder of encoders) {
            expect(encoder).to.have.property('title');
            expect(encoder).to.have.property('name');
        }

        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Get available encoders for advanced streaming', async () => {
        const stream = osn.AdvancedStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the advanced streaming output");

        stream.service = osn.ServiceFactory.legacySettings;

        const encoders = stream.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");
        expect(encoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder");

        for (const encoder of encoders) {
            expect(encoder).to.have.property('title');
            expect(encoder).to.have.property('name');
            expect(encoder.title).to.be.a('string',
                "Encoder title should be a string");
            expect(encoder.name).to.be.a('string',
                "Encoder name should be a string");
            expect(encoder.title).to.not.equal('',
                "Encoder title should not be empty");
            expect(encoder.name).to.not.equal('',
                "Encoder name should not be empty");
        }

        osn.AdvancedStreamingFactory.destroy(stream);
    });

    it('Get available encoders for advanced streaming without service', async () => {
        const stream = osn.AdvancedStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the advanced streaming output");

        const encoders = stream.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");

        for (const encoder of encoders) {
            expect(encoder).to.have.property('title');
            expect(encoder).to.have.property('name');
        }

        osn.AdvancedStreamingFactory.destroy(stream);
    });

    it('Get available AV1 encoders for custom RTMP streaming using output codec fallback', async () => {
        const youtubeService = osn.ServiceFactory.create('rtmp_common', 'youtube-service', {
            service: 'YouTube - RTMPS',
            server: 'rtmps://a.rtmps.youtube.com:443/live2',
            key: 'test',
        });
        const customService = osn.ServiceFactory.create('rtmp_custom', 'custom-service', {
            server: 'rtmps://a.rtmps.youtube.com:443/live2',
            key: 'test',
        });
        const youtubeStream = osn.AdvancedStreamingFactory.create();
        const customStream = osn.AdvancedStreamingFactory.create();

        try {
            youtubeStream.service = youtubeService;
            customStream.service = customService;

            const youtubeAv1Encoders = getAv1EncoderNames(youtubeStream.getAvailableEncoders());
            const customEncoderNames = getEncoderNames(customStream.getAvailableEncoders());

            expect(youtubeAv1Encoders.length).to.be.greaterThan(0,
                'Test requires at least one registered AV1 encoder for YouTube');

            for (const encoder of youtubeAv1Encoders) {
                expect(customEncoderNames).to.include(encoder,
                    `Custom RTMP service should allow ${encoder} when the output supports AV1`);
            }
        } finally {
            osn.AdvancedStreamingFactory.destroy(customStream);
            osn.AdvancedStreamingFactory.destroy(youtubeStream);
            osn.ServiceFactory.destroy(customService);
            osn.ServiceFactory.destroy(youtubeService);
        }
    });

    it('Get available encoders for simple recording', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        expect(recording).to.not.equal(
            undefined, "Error while creating the simple recording output");

        recording.video = obs.defaultVideoContext;

        const encoders = recording.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");
        expect(encoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder");

        for (const encoder of encoders) {
            expect(encoder).to.have.property('title');
            expect(encoder).to.have.property('name');
            expect(encoder.title).to.be.a('string',
                "Encoder title should be a string");
            expect(encoder.name).to.be.a('string',
                "Encoder name should be a string");
            expect(encoder.title).to.not.equal('',
                "Encoder title should not be empty");
            expect(encoder.name).to.not.equal('',
                "Encoder name should not be empty");
        }

        osn.SimpleRecordingFactory.destroy(recording);
    });

    it('Get available encoders for simple recording with different formats', async () => {
        const recording = osn.SimpleRecordingFactory.create();
        expect(recording).to.not.equal(
            undefined, "Error while creating the simple recording output");

        recording.video = obs.defaultVideoContext;

        // MP4 format
        recording.format = ERecordingFormat.MP4;
        const mp4Encoders = recording.getAvailableEncoders();
        expect(mp4Encoders).to.be.an('array',
            "getAvailableEncoders should return an array for MP4");
        expect(mp4Encoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder for MP4");

        // MKV format - supports all codecs
        recording.format = ERecordingFormat.MKV;
        const mkvEncoders = recording.getAvailableEncoders();
        expect(mkvEncoders).to.be.an('array',
            "getAvailableEncoders should return an array for MKV");
        expect(mkvEncoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder for MKV");

        // MKV supports all codecs, so it should have at least as many as MP4
        expect(mkvEncoders.length).to.be.greaterThanOrEqual(mp4Encoders.length,
            "MKV should support at least as many encoders as MP4");

        // FLV format - only supports h264
        recording.format = ERecordingFormat.FLV;
        const flvEncoders = recording.getAvailableEncoders();
        expect(flvEncoders).to.be.an('array',
            "getAvailableEncoders should return an array for FLV");

        // FLV only supports h264, so should have fewer encoders than MKV
        expect(flvEncoders.length).to.be.lessThanOrEqual(mkvEncoders.length,
            "FLV should support fewer or equal encoders compared to MKV");

        osn.SimpleRecordingFactory.destroy(recording);
    });

    it('Get available encoders for advanced recording', async () => {
        const recording = osn.AdvancedRecordingFactory.create();
        expect(recording).to.not.equal(
            undefined, "Error while creating the advanced recording output");

        recording.video = obs.defaultVideoContext;

        const encoders = recording.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");
        expect(encoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder");

        for (const encoder of encoders) {
            expect(encoder).to.have.property('title');
            expect(encoder).to.have.property('name');
            expect(encoder.title).to.be.a('string',
                "Encoder title should be a string");
            expect(encoder.name).to.be.a('string',
                "Encoder name should be a string");
            expect(encoder.title).to.not.equal('',
                "Encoder title should not be empty");
            expect(encoder.name).to.not.equal('',
                "Encoder name should not be empty");
        }

        osn.AdvancedRecordingFactory.destroy(recording);
    });

    it('Get available encoders for advanced recording with different formats', async () => {
        const recording = osn.AdvancedRecordingFactory.create();
        expect(recording).to.not.equal(
            undefined, "Error while creating the advanced recording output");

        recording.video = obs.defaultVideoContext;

        // MP4 format
        recording.format = ERecordingFormat.MP4;
        const mp4Encoders = recording.getAvailableEncoders();
        expect(mp4Encoders).to.be.an('array',
            "getAvailableEncoders should return an array for MP4");
        expect(mp4Encoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder for MP4");

        // MKV format - supports all codecs
        recording.format = ERecordingFormat.MKV;
        const mkvEncoders = recording.getAvailableEncoders();
        expect(mkvEncoders).to.be.an('array',
            "getAvailableEncoders should return an array for MKV");
        expect(mkvEncoders.length).to.be.greaterThan(0,
            "Should have at least one available encoder for MKV");

        expect(mkvEncoders.length).to.be.greaterThanOrEqual(mp4Encoders.length,
            "MKV should support at least as many encoders as MP4");

        osn.AdvancedRecordingFactory.destroy(recording);
    });
});
