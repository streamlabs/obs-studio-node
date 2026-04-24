import * as fs from 'fs';
import * as path from 'path';
import { ITimeSpec } from '../osn';

// OBS types
const basicOBSInputTypes: string[] = ['image_source', 'color_source', 'slideshow', 'browser_source', 'ffmpeg_source', 'text_gdiplus', 'text_ft2_source',
                                      'monitor_capture', 'window_capture', 'game_capture', 'dshow_input', 'wasapi_input_capture', 'wasapi_output_capture'];
export {basicOBSInputTypes};

const basicDebugOBSInputTypes: string[] = ['image_source', 'color_source', 'slideshow', 'ffmpeg_source', 'text_gdiplus', 'text_ft2_source',
                                           'monitor_capture', 'window_capture', 'game_capture', 'dshow_input', 'wasapi_input_capture', 'wasapi_output_capture'];
export {basicDebugOBSInputTypes};

let basicOBSFilterTypes: string[] = ['mask_filter', 'crop_filter', 'gain_filter', 'color_filter', 'scale_filter', 'scroll_filter', 'gpu_delay',
                                     'color_key_filter', 'clut_filter', 'sharpness_filter', 'chroma_key_filter', 'async_delay_filter', 'noise_suppress_filter',
                                     'invert_polarity_filter', 'noise_gate_filter', 'compressor_filter', "limiter_filter", 'expander_filter', 'vst_filter'];
export {basicOBSFilterTypes};

let basicOBSTransitionTypes: string[] = ['cut_transition', 'fade_transition', 'swipe_transition', 'slide_transition', 'fade_to_color_transition', 'wipe_transition', 'obs_stinger_transition'];
export {basicOBSTransitionTypes};

let basicOBSSettingsCategories: string[] = ['General', 'Stream', 'Output', 'Audio', 'Video', 'Hotkeys', 'Advanced'];
export {basicOBSSettingsCategories}

// OBS hotkeys
let showHideInputHotkeys: string[] = ['SHOW_SCENE_ITEM.WASAPI_OUTPUT_CAPTURE', 'HIDE_SCENE_ITEM.WASAPI_OUTPUT_CAPTURE', 'SHOW_SCENE_ITEM.WASAPI_INPUT_CAPTURE',
'HIDE_SCENE_ITEM.WASAPI_INPUT_CAPTURE', 'SHOW_SCENE_ITEM.DSHOW_INPUT', 'HIDE_SCENE_ITEM.DSHOW_INPUT', 'SHOW_SCENE_ITEM.GAME_CAPTURE', 'HIDE_SCENE_ITEM.GAME_CAPTURE',
'SHOW_SCENE_ITEM.WINDOW_CAPTURE', 'HIDE_SCENE_ITEM.WINDOW_CAPTURE', 'SHOW_SCENE_ITEM.MONITOR_CAPTURE', 'HIDE_SCENE_ITEM.MONITOR_CAPTURE', 'SHOW_SCENE_ITEM.TEXT_FT2_SOURCE',
'HIDE_SCENE_ITEM.TEXT_FT2_SOURCE', 'SHOW_SCENE_ITEM.TEXT_GDIPLUS', 'HIDE_SCENE_ITEM.TEXT_GDIPLUS', 'SHOW_SCENE_ITEM.BROWSER_SOURCE', 'HIDE_SCENE_ITEM.BROWSER_SOURCE',
'SHOW_SCENE_ITEM.COLOR_SOURCE', 'HIDE_SCENE_ITEM.COLOR_SOURCE', 'SHOW_SCENE_ITEM.IMAGE_SOURCE', 'HIDE_SCENE_ITEM.IMAGE_SOURCE', 'SHOW_SCENE_ITEM.SLIDESHOW',
'HIDE_SCENE_ITEM.SLIDESHOW', 'SHOW_SCENE_ITEM.FFMPEG_SOURCE', 'HIDE_SCENE_ITEM.FFMPEG_SOURCE', 'SHOW_SCENE_ITEM.AUDIO_LINE', 'HIDE_SCENE_ITEM.AUDIO_LINE',
'SHOW_SCENE_ITEM.VLC_SOURCE', 'HIDE_SCENE_ITEM.VLC_SOURCE', 'SHOW_SCENE_ITEM.NDI_SOURCE', 'HIDE_SCENE_ITEM.NDI_SOURCE',
'SHOW_SCENE_ITEM.COREAUDIO_OUTPUT_CAPTURE', 'HIDE_SCENE_ITEM.COREAUDIO_OUTPUT_CAPTURE',
'SHOW_SCENE_ITEM.COREAUDIO_INPUT_CAPTURE', 'HIDE_SCENE_ITEM.COREAUDIO_INPUT_CAPTURE', 'SHOW_SCENE_ITEM.AV_CAPTURE_INPUT', 'HIDE_SCENE_ITEM.AV_CAPTURE_INPUT',
'SHOW_SCENE_ITEM.DISPLAY_CAPTURE', 'HIDE_SCENE_ITEM.DISPLAY_CAPTURE', 'SHOW_SCENE_ITEM.TEXT_FT2_SOURCE_V2', 'HIDE_SCENE_ITEM.TEXT_FT2_SOURCE_V2',
'SHOW_SCENE_ITEM.TEXT_GDIPLUS_V2', 'HIDE_SCENE_ITEM.TEXT_GDIPLUS_V2', 'SHOW_SCENE_ITEM.OPENVR_CAPTURE', 'HIDE_SCENE_ITEM.OPENVR_CAPTURE',
'SHOW_SCENE_ITEM.COLOR_SOURCE_V2', 'HIDE_SCENE_ITEM.COLOR_SOURCE_V2', 'SHOW_SCENE_ITEM.COLOR_SOURCE_V3', 'HIDE_SCENE_ITEM.COLOR_SOURCE_V3',
'SHOW_SCENE_ITEM.SCREEN_CAPTURE', 'HIDE_SCENE_ITEM.SCREEN_CAPTURE', 'SHOW_SCENE_ITEM.MEDIASOUPCONNECTOR', 'HIDE_SCENE_ITEM.MEDIASOUPCONNECTOR',
'SHOW_SCENE_ITEM.WASAPI_PROCESS_OUTPUT_CAPTURE', 'HIDE_SCENE_ITEM.WASAPI_PROCESS_OUTPUT_CAPTURE', 'SHOW_SCENE_ITEM.AV_CAPTURE_INPUT_V2',
'HIDE_SCENE_ITEM.AV_CAPTURE_INPUT_V2','SHOW_SCENE_ITEM.SPOUT_CAPTURE','HIDE_SCENE_ITEM.SPOUT_CAPTURE',
'SHOW_SCENE_ITEM.SLIDESHOW_V2', 'HIDE_SCENE_ITEM.SLIDESHOW_V2', 'SHOW_SCENE_ITEM.TEXT_GDIPLUS_V3', 'HIDE_SCENE_ITEM.TEXT_GDIPLUS_V3',
'SHOW_SCENE_ITEM.MACOS_AVCAPTURE', 'HIDE_SCENE_ITEM.MACOS_AVCAPTURE', 'SHOW_SCENE_ITEM.MACOS_AVCAPTURE_FAST', 'HIDE_SCENE_ITEM.MACOS_AVCAPTURE_FAST',
'SHOW_SCENE_ITEM.SCK_AUDIO_CAPTURE', 'HIDE_SCENE_ITEM.SCK_AUDIO_CAPTURE', 'SHOW_SCENE_ITEM.MAC_SCREEN_CAPTURE', 'HIDE_SCENE_ITEM.MAC_SCREEN_CAPTURE'];

export {showHideInputHotkeys};

let slideshowHotkeys: string[] = ['PLAYPAUSE', 'RESTART', 'STOP', 'NEXTSLIDE', 'PREVIOUSSLIDE'];
export {slideshowHotkeys};

let captureSourceHotkeys: string[] = [];
export {captureSourceHotkeys};

let ffmpeg_sourceHotkeys: string[] = ['MUTE', 'UNMUTE', 'PUSH_TO_MUTE', 'PUSH_TO_TALK', 'RESTART', 'PLAY', 'PAUSE', 'STOP'];
export {ffmpeg_sourceHotkeys};

let game_captureHotkeys: string[] = ['MUTE', 'UNMUTE', 'PUSH_TO_MUTE', 'PUSH_TO_TALK', 'HOTKEY_START', 'HOTKEY_STOP'];
export {game_captureHotkeys};

let dshow_wasapitHotkeys: string[] = ['MUTE', 'UNMUTE', 'PUSH_TO_MUTE', 'PUSH_TO_TALK'];
export {dshow_wasapitHotkeys};

let coreaudioHotkeys: string[] = ['MUTE', 'UNMUTE', 'PUSH_TO_MUTE', 'PUSH_TO_TALK'];
export {coreaudioHotkeys};

// Helper functions
export function getTimeSpec(ms: number): ITimeSpec {
    return {
        sec: Math.floor(ms / 1000),
        nsec: Math.floor(ms % 1000) * 1000000,
    };
}

export function getCppErrorMsg(errorStack: any): string {
    return errorStack.stack.split("\n", 1).join("").substring(7);
}

export function deleteConfigFiles(): void {
    const configFolderPath = path.join(path.normalize(__dirname), '..', 'osnData/slobs-client');
    if (fs.existsSync(configFolderPath)) {
        let currentFile: string;
        let files = fs.readdirSync(configFolderPath);
        files.forEach(file => {
            // Ignore mac Crashpad and node-obs/logs folders.
            if (file !== 'node-obs' && file !== 'Crashpad') {
                currentFile = file;
                try {
                    fs.unlinkSync(path.join(configFolderPath, file));
                } catch(error) {
                    if (error.code === "EBUSY") {
                        throw ('Error: the file ' + currentFile + ' or slobs-client folder is busy');
                    }
                }
            }
        });
    }
}

export function getRandomValue(list: any) {
    const value = list[Math.floor(Math.random() * list.length)];
    return value[Object.keys(value)[0]];
}

export function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

export async function waitForFile(filePath: string, timeoutMs: number = 5000, pollIntervalMs: number = 100): Promise<void> {
    const timeoutAt = Date.now() + timeoutMs;
    while (Date.now() < timeoutAt) {
        if (fs.existsSync(filePath)) {
            return;
        }
        await sleep(pollIntervalMs);
    }

    throw Error(`Timed out waiting for file: ${filePath}`);
}
