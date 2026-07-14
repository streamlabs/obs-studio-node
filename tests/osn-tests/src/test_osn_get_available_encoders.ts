import 'mocha'
import { expect } from 'chai'
import * as osn from '../osn';
import { logInfo, logEmptyLine } from '../util/logger';
import { OBSHandler } from '../util/obs_handler'
import { deleteConfigFiles } from '../util/general';
import { EOBSSettingsCategories } from '../util/obs_enums';
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

function expectEncoderMetadataValues(
    encoders: any[],
    name: string,
    expected: { family?: string, preset?: string, id?: string },
) {
    const encoder = encoders.find(encoder => encoder.name === name);
    if (!encoder) return;

    if (expected.family) {
        expect(encoder.family).to.equal(expected.family,
            `${name} should expose public family metadata`);
    }

    if (expected.preset) {
        expect(encoder.preset).to.equal(expected.preset,
            `${name} should expose the public preset field metadata`);
    }

    if (expected.id) {
        expect(encoder.id).to.equal(expected.id,
            `${name} should expose the concrete OBS encoder id`);
    }
}

function expectEncoderMetadata(encoder: any) {
    expect(encoder).to.have.property('title');
    expect(encoder).to.have.property('name');
    expect(encoder).to.have.property('id');
    expect(encoder).to.have.property('family');
    expect(encoder).to.have.property('preset');
    expect(encoder).to.have.property('codec');
    expect(encoder).to.have.property('streaming');
    expect(encoder).to.have.property('recording');

    expect(encoder.title).to.be.a('string',
        "Encoder title should be a string");
    expect(encoder.name).to.be.a('string',
        "Encoder name should be a string");
    expect(encoder.id).to.be.a('string',
        "Encoder id should be a string");
    expect(encoder.family).to.be.a('string',
        "Encoder family should be a string");
    expect(encoder.preset).to.be.a('string',
        "Encoder preset should be a string");
    expect(encoder.codec).to.be.a('string',
        "Encoder codec should be a string");
    expect(encoder.streaming).to.be.a('boolean',
        "Encoder streaming support should be a boolean");
    expect(encoder.recording).to.be.a('boolean',
        "Encoder recording support should be a boolean");

    expect(encoder.title).to.not.equal('',
        "Encoder title should not be empty");
    expect(encoder.name).to.not.equal('',
        "Encoder name should not be empty");
    expect(encoder.id).to.not.equal('',
        "Encoder id should not be empty");
    expect(encoder.family).to.not.equal('',
        "Encoder family should not be empty");
    expect(encoder.family).to.not.match(/^family_/,
        "Encoder family should be public metadata, not an internal backend family constant");
    expect(encoder.preset).to.not.equal('',
        "Encoder preset should not be empty");
    expect(encoder.codec).to.not.equal('',
        "Encoder codec should not be empty");
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
            expectEncoderMetadata(encoder);
        }

        const x264 = encoders.find(encoder => encoder.id === 'obs_x264' && encoder.family === 'x264');
        expect(x264).to.not.equal(undefined,
            "Simple streaming encoders should include the software x264 encoder");
        expect((x264 as any).id).to.equal('obs_x264',
            "Simple x264 should expose the concrete OBS encoder id");
        expect((x264 as any).family).to.equal('x264',
            "Simple x264 should expose public family metadata");
        expect((x264 as any).preset).to.equal('Preset',
            "Simple x264 should expose the simple output preset field");

        osn.SimpleStreamingFactory.destroy(stream);
    });

    it('Simple streaming metadata ids create matching encoders in Simple mode', async function() {
        const originalMode = obs.getSetting(EOBSSettingsCategories.Output, 'Mode');
        obs.setSetting(EOBSSettingsCategories.Output, 'Mode', 'Simple');

        let stream: osn.ISimpleStreaming | undefined;
        const createdEncoders: osn.IVideoEncoder[] = [];

        try {
            stream = osn.SimpleStreamingFactory.create();
            expect(stream).to.not.equal(
                undefined, "Error while creating the simple streaming output");

            stream.service = osn.ServiceFactory.legacySettings;

            const encoders = stream.getAvailableEncoders();
            for (const metadata of encoders) {
                const encoder = osn.VideoEncoderFactory.create(
                    metadata.id,
                    `video-encoder-metadata-id-${metadata.name}`,
                    {},
                );
                createdEncoders.push(encoder);

                expect(encoder.id).to.equal(metadata.id,
                    `${metadata.name} metadata id should create ${metadata.id} in Simple mode`);
            }
        } finally {
            for (const encoder of createdEncoders) {
                encoder.release();
            }

            if (stream) {
                osn.SimpleStreamingFactory.destroy(stream);
            }

            if (originalMode !== undefined) {
                obs.setSetting(EOBSSettingsCategories.Output, 'Mode', originalMode);
            }
        }
    });

    it('Get available encoders for simple streaming without service', async () => {
        const stream = osn.SimpleStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the simple streaming output");

        const encoders = stream.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");

        for (const encoder of encoders) {
            expectEncoderMetadata(encoder);
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
            expectEncoderMetadata(encoder);
            expect((encoder as any).id).to.equal(encoder.name,
                "Advanced encoder metadata should expose name as the concrete OBS encoder id");
        }

        expectEncoderMetadataValues(encoders, 'obs_x264', {
            family: 'x264',
            preset: 'preset',
        });
        expectEncoderMetadataValues(encoders, 'obs_qsv11_v2', {
            family: 'qsv',
            preset: 'target_usage',
        });
        expectEncoderMetadataValues(encoders, 'h264_texture_amf', {
            family: 'amd',
            preset: 'preset',
        });
        expectEncoderMetadataValues(encoders, 'ffmpeg_nvenc', {
            family: 'nvenc',
            preset: 'preset2',
        });
        expectEncoderMetadataValues(encoders, 'com.apple.videotoolbox.videoencoder.h264.gva', {
            family: 'apple',
            preset: 'profile',
        });

        osn.AdvancedStreamingFactory.destroy(stream);
    });

    it('Advanced streaming preset metadata points at concrete OBS encoder properties', async () => {
        const stream = osn.AdvancedStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the advanced streaming output");

        stream.service = osn.ServiceFactory.legacySettings;
        const createdEncoders: osn.IVideoEncoder[] = [];

        try {
            const encoders = stream.getAvailableEncoders();
            expect(encoders).to.be.an('array',
                "getAvailableEncoders should return an array");
            expect(encoders.length).to.be.greaterThan(0,
                "Should have at least one available encoder");

            for (const metadata of encoders) {
                const encoder = osn.VideoEncoderFactory.create(
                    metadata.id,
                    `video-encoder-preset-metadata-${metadata.name}`,
                    {},
                );
                createdEncoders.push(encoder);

                const propertyNames: string[] = [];
                let prop: any = encoder.properties.first();
                while (prop) {
                    propertyNames.push(prop.name);
                    prop = prop.next();
                }

                expect(propertyNames).to.include(metadata.preset,
                    `${metadata.name} preset metadata should reference an OBS encoder property`);
            }
        } finally {
            for (const encoder of createdEncoders) {
                encoder.release();
            }

            osn.AdvancedStreamingFactory.destroy(stream);
        }
    });

    it('Get available encoders for advanced streaming without service', async () => {
        const stream = osn.AdvancedStreamingFactory.create();
        expect(stream).to.not.equal(
            undefined, "Error while creating the advanced streaming output");

        const encoders = stream.getAvailableEncoders();
        expect(encoders).to.be.an('array',
            "getAvailableEncoders should return an array");

        for (const encoder of encoders) {
            expectEncoderMetadata(encoder);
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
            expectEncoderMetadata(encoder);
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
            expectEncoderMetadata(encoder);
            expect((encoder as any).id).to.equal(encoder.name,
                "Advanced encoder metadata should expose name as the concrete OBS encoder id");
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
