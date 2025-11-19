import * as path from 'path';
import * as fs from 'fs';
const hasDeveloperApp = fs.existsSync(path.join(__dirname, 'OSN.app')); // search for local developer OSN.app bundle which stores CEF helper apps, etc
const obs = hasDeveloperApp
    ? require('./OSN.app/distribute/obs-studio-node/obs_studio_client.node')
    : require('./obs_studio_client.node');
/* Convenient paths to modules */
export const DefaultD3D11Path = path.resolve(__dirname, `libobs-d3d11.dll`);
export const DefaultOpenGLPath = path.resolve(__dirname, `libobs-opengl.dll`);
export const DefaultDrawPluginPath = path.resolve(__dirname, `simple_draw.dll`);
export const DefaultBinPath = path.resolve(__dirname);
export const DefaultDataPath = path.resolve(__dirname, `data`);
export const DefaultPluginPath = path.resolve(__dirname, `obs-plugins`);
export const DefaultPluginDataPath = path.resolve(__dirname, `data/obs-plugins/%module%`);
export const DefaultPluginPathMac = path.resolve(__dirname, `PlugIns`);
export const Global = obs.Global;
export const Video = obs.Video;
export const VideoFactory = obs.Video;
export const InputFactory = obs.Input;
export const SceneFactory = obs.Scene;
export const FilterFactory = obs.Filter;
export const TransitionFactory = obs.Transition;
export const DisplayFactory = obs.Display;
export const VolmeterFactory = obs.Volmeter;
export const FaderFactory = obs.Fader;
export const Audio = obs.Audio;
export const AudioFactory = obs.Audio;
export const ModuleFactory = obs.Module;
export const IPC = obs.IPC;
export const VideoEncoderFactory = obs.VideoEncoder;
export const ServiceFactory = obs.Service;
export const SimpleStreamingFactory = obs.SimpleStreaming;
export const AdvancedStreamingFactory = obs.AdvancedStreaming;
export const DelayFactory = obs.Delay;
export const ReconnectFactory = obs.Reconnect;
export const NetworkFactory = obs.Network;
export const AudioTrackFactory = obs.AudioTrack;
export const SimpleRecordingFactory = obs.SimpleRecording;
export const AdvancedRecordingFactory = obs.AdvancedRecording;
export const AudioEncoderFactory = obs.AudioEncoder;
export const SimpleReplayBufferFactory = obs.SimpleReplayBuffer;
export const AdvancedReplayBufferFactory = obs.AdvancedReplayBuffer;
;
;
;
;
export function addItems(scene, sceneItems) {
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
export function createSources(sources) {
    const items = [];
    if (Array.isArray(sources)) {
        sources.forEach(function (source) {
            let newSource = null;
            try {
                newSource = obs.Input.create(source.type, source.name, source.settings);
            }
            catch (error) {
                console.error(`[OSN] Failed to create input for source "${source.name}":`, error instanceof Error ? error.message : error);
                return; // Skip the rest of this iteration if input creation fails
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
export function getSourcesSize(sourcesNames) {
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
// Initialization and other stuff which needs local data.
const __dirnameApple = hasDeveloperApp
    ? path.join(__dirname, 'OSN.app', 'distribute', 'obs-studio-node', 'bin')
    : path.join(__dirname, 'bin');
if (fs.existsSync(path.resolve(__dirnameApple).replace('app.asar', 'app.asar.unpacked'))) {
    obs.IPC.setServerPath(path.resolve(__dirnameApple, `obs64`).replace('app.asar', 'app.asar.unpacked'), path.resolve(__dirnameApple).replace('app.asar', 'app.asar.unpacked'));
}
else if (fs.existsSync(path.resolve(__dirname, `obs64.exe`).replace('app.asar', 'app.asar.unpacked'))) {
    obs.IPC.setServerPath(path.resolve(__dirname, `obs64.exe`).replace('app.asar', 'app.asar.unpacked'), path.resolve(__dirname).replace('app.asar', 'app.asar.unpacked'));
}
else {
    obs.IPC.setServerPath(path.resolve(__dirname, `obs32.exe`).replace('app.asar', 'app.asar.unpacked'), path.resolve(__dirname).replace('app.asar', 'app.asar.unpacked'));
}
export const NodeObs = obs;
