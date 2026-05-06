"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.NodeObs = exports.AdvancedReplayBufferFactory = exports.SimpleReplayBufferFactory = exports.AudioEncoderFactory = exports.AdvancedRecordingFactory = exports.SimpleRecordingFactory = exports.AudioTrackFactory = exports.NetworkFactory = exports.ReconnectFactory = exports.DelayFactory = exports.EnhancedBroadcastingSimpleStreamingFactory = exports.EnhancedBroadcastingAdvancedStreamingFactory = exports.AdvancedStreamingFactory = exports.SimpleStreamingFactory = exports.ServiceFactory = exports.VideoEncoderFactory = exports.IPC = exports.ModuleFactory = exports.AudioFactory = exports.Audio = exports.FaderFactory = exports.VolmeterFactory = exports.DisplayFactory = exports.TransitionFactory = exports.FilterFactory = exports.SceneFactory = exports.InputFactory = exports.VideoFactory = exports.Video = exports.Global = exports.DefaultPluginPathMac = exports.DefaultPluginDataPath = exports.DefaultPluginPath = exports.DefaultDataPath = exports.DefaultBinPath = exports.DefaultDrawPluginPath = exports.DefaultOpenGLPath = exports.DefaultD3D11Path = void 0;
exports.addItems = addItems;
exports.createSources = createSources;
exports.getSourcesSize = getSourcesSize;
const path = require("path");
const fs = require("fs");
const hasDeveloperApp = process.platform === 'darwin' && fs.existsSync(path.join(__dirname, 'OSN.app'));
const obs = hasDeveloperApp
    ? require('./OSN.app/distribute/obs-studio-node/obs_studio_client.node')
    : require('./obs_studio_client.node');
exports.DefaultD3D11Path = path.resolve(__dirname, `libobs-d3d11.dll`);
exports.DefaultOpenGLPath = path.resolve(__dirname, `libobs-opengl.dll`);
exports.DefaultDrawPluginPath = path.resolve(__dirname, `simple_draw.dll`);
exports.DefaultBinPath = path.resolve(__dirname);
exports.DefaultDataPath = path.resolve(__dirname, `data`);
exports.DefaultPluginPath = path.resolve(__dirname, `obs-plugins`);
exports.DefaultPluginDataPath = path.resolve(__dirname, `data/obs-plugins/%module%`);
exports.DefaultPluginPathMac = path.resolve(__dirname, `PlugIns`);
exports.Global = obs.Global;
exports.Video = obs.Video;
exports.VideoFactory = obs.Video;
exports.InputFactory = obs.Input;
exports.SceneFactory = obs.Scene;
exports.FilterFactory = obs.Filter;
exports.TransitionFactory = obs.Transition;
exports.DisplayFactory = obs.Display;
exports.VolmeterFactory = obs.Volmeter;
exports.FaderFactory = obs.Fader;
exports.Audio = obs.Audio;
exports.AudioFactory = obs.Audio;
exports.ModuleFactory = obs.Module;
exports.IPC = obs.IPC;
exports.VideoEncoderFactory = obs.VideoEncoder;
exports.ServiceFactory = obs.Service;
exports.SimpleStreamingFactory = obs.SimpleStreaming;
exports.AdvancedStreamingFactory = obs.AdvancedStreaming;
exports.EnhancedBroadcastingAdvancedStreamingFactory = obs.EnhancedBroadcastingAdvancedStreaming;
exports.EnhancedBroadcastingSimpleStreamingFactory = obs.EnhancedBroadcastingSimpleStreaming;
exports.DelayFactory = obs.Delay;
exports.ReconnectFactory = obs.Reconnect;
exports.NetworkFactory = obs.Network;
exports.AudioTrackFactory = obs.AudioTrack;
exports.SimpleRecordingFactory = obs.SimpleRecording;
exports.AdvancedRecordingFactory = obs.AdvancedRecording;
exports.AudioEncoderFactory = obs.AudioEncoder;
exports.SimpleReplayBufferFactory = obs.SimpleReplayBuffer;
exports.AdvancedReplayBufferFactory = obs.AdvancedReplayBuffer;
;
;
;
;
function addItems(scene, sceneItems) {
    const items = [];
    if (Array.isArray(sceneItems)) {
        sceneItems.forEach(function (sceneItem) {
            const source = obs.Input.fromName(sceneItem.name);
            const item = scene.add(source, sceneItem);
            items.push(item);
        });
    }
    return items;
}
function createSources(sources) {
    const items = [];
    if (Array.isArray(sources)) {
        sources.forEach(function (source) {
            let newSource = null;
            try {
                newSource = obs.Input.create(source.type, source.name, source.settings);
            }
            catch (error) {
                console.error(`[OSN] Failed to create input for source "${source.name}":`, error instanceof Error ? error.message : error);
                return;
            }
            if (newSource) {
                if (newSource.audioMixers) {
                    newSource.muted = source.muted ?? false;
                    newSource.volume = source.volume ?? 1;
                    newSource.syncOffset = source.syncOffset ?? { sec: 0, nsec: 0 };
                }
                newSource.deinterlaceMode = source.deinterlaceMode;
                newSource.deinterlaceFieldOrder = source.deinterlaceFieldOrder;
                items.push(newSource);
                const filters = source.filters;
                if (Array.isArray(filters)) {
                    filters.forEach(function (filter) {
                        let ObsFilter = null;
                        try {
                            ObsFilter = obs.Filter.create(filter.type, filter.name, filter.settings);
                        }
                        catch (filterError) {
                            console.error(`[OSN] Failed to create filter "${filter.name}" for source "${source.name}":`, filterError instanceof Error ? filterError.message : filterError);
                        }
                        if (ObsFilter) {
                            ObsFilter.enabled = filter.enabled ?? true;
                            newSource.addFilter(ObsFilter);
                            ObsFilter.release();
                        }
                    });
                }
            }
            else {
                console.warn(`[OSN] Input creation failed for source: ${source.name}`);
            }
        });
    }
    else {
        console.error(`[OSN] Invalid sources array provided:`, sources);
    }
    return items;
}
function getSourcesSize(sourcesNames) {
    const sourcesSize = [];
    if (Array.isArray(sourcesNames)) {
        sourcesNames.forEach(function (sourceName) {
            const ObsInput = obs.Input.fromName(sourceName);
            if (ObsInput) {
                sourcesSize.push({ name: sourceName, height: ObsInput.height, width: ObsInput.width, outputFlags: ObsInput.outputFlags });
            }
        });
    }
    return sourcesSize;
}
;
const appleBinaryFolder = hasDeveloperApp
    ? path.join(__dirname, 'OSN.app', 'distribute', 'obs-studio-node', 'bin')
    : path.join(__dirname, 'bin');
if (fs.existsSync(path.resolve(appleBinaryFolder, 'obs64').replace('app.asar', 'app.asar.unpacked'))) {
    obs.IPC.setServerPath(path.resolve(appleBinaryFolder, `obs64`).replace('app.asar', 'app.asar.unpacked'), path.resolve(appleBinaryFolder).replace('app.asar', 'app.asar.unpacked'));
}
else if (fs.existsSync(path.resolve(__dirname, `obs64.exe`).replace('app.asar', 'app.asar.unpacked'))) {
    obs.IPC.setServerPath(path.resolve(__dirname, `obs64.exe`).replace('app.asar', 'app.asar.unpacked'), path.resolve(__dirname).replace('app.asar', 'app.asar.unpacked'));
}
else {
    obs.IPC.setServerPath(path.resolve(__dirname, `obs32.exe`).replace('app.asar', 'app.asar.unpacked'), path.resolve(__dirname).replace('app.asar', 'app.asar.unpacked'));
}
exports.NodeObs = obs;
