"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.SceneFactory = exports.InputFactory = exports.VideoFactory = exports.Video = exports.Global = exports.ERecSplitType = exports.EVcamInstalledStatus = exports.EIPCError = exports.ERenderingMode = exports.ECategoryTypes = exports.EOutputCode = exports.ESpeakerLayout = exports.EColorSpace = exports.EBoundsType = exports.EVideoFormat = exports.ERangeType = exports.EFPSType = exports.EScaleType = exports.EColorFormat = exports.EFaderType = exports.ESourceType = exports.ESceneDupType = exports.ESourceOutputFlags = exports.EOutputFlags = exports.EAlignment = exports.ENumberType = exports.ETextInfoType = exports.ETextType = exports.EPathType = exports.EEditableListType = exports.EListFormat = exports.EPropertyType = exports.EFontStyle = exports.EBlendingMode = exports.EBlendingMethod = exports.EDeinterlaceMode = exports.EHotkeyObjectType = exports.EVideoCodes = exports.EDeinterlaceFieldOrder = exports.EOrderMovement = exports.EMonitoringType = exports.ESourceFlags = exports.DefaultPluginPathMac = exports.DefaultPluginDataPath = exports.DefaultPluginPath = exports.DefaultDataPath = exports.DefaultBinPath = exports.DefaultDrawPluginPath = exports.DefaultOpenGLPath = exports.DefaultD3D11Path = void 0;
exports.NodeObs = exports.VCamOutputType = exports.EProcessPriority = exports.EVideoEncoderType = exports.ERecordingQuality = exports.ERecordingFormat = exports.EMouseButtonType = exports.EInteractionFlags = exports.AdvancedReplayBufferFactory = exports.SimpleReplayBufferFactory = exports.AudioEncoderFactory = exports.AdvancedRecordingFactory = exports.SimpleRecordingFactory = exports.AudioTrackFactory = exports.NetworkFactory = exports.ReconnectFactory = exports.DelayFactory = exports.EnhancedBroadcastingSimpleStreamingFactory = exports.EnhancedBroadcastingAdvancedStreamingFactory = exports.AdvancedStreamingFactory = exports.SimpleStreamingFactory = exports.ServiceFactory = exports.VideoEncoderFactory = exports.IPC = exports.ModuleFactory = exports.AudioFactory = exports.Audio = exports.FaderFactory = exports.VolmeterFactory = exports.TransitionFactory = exports.FilterFactory = void 0;
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
var ESourceFlags;
(function (ESourceFlags) {
    ESourceFlags[ESourceFlags["Unbuffered"] = 1] = "Unbuffered";
    ESourceFlags[ESourceFlags["ForceMono"] = 2] = "ForceMono";
})(ESourceFlags || (exports.ESourceFlags = ESourceFlags = {}));
var EMonitoringType;
(function (EMonitoringType) {
    EMonitoringType[EMonitoringType["None"] = 0] = "None";
    EMonitoringType[EMonitoringType["MonitoringOnly"] = 1] = "MonitoringOnly";
    EMonitoringType[EMonitoringType["MonitoringAndOutput"] = 2] = "MonitoringAndOutput";
})(EMonitoringType || (exports.EMonitoringType = EMonitoringType = {}));
var EOrderMovement;
(function (EOrderMovement) {
    EOrderMovement[EOrderMovement["Up"] = 0] = "Up";
    EOrderMovement[EOrderMovement["Down"] = 1] = "Down";
    EOrderMovement[EOrderMovement["Top"] = 2] = "Top";
    EOrderMovement[EOrderMovement["Bottom"] = 3] = "Bottom";
})(EOrderMovement || (exports.EOrderMovement = EOrderMovement = {}));
var EDeinterlaceFieldOrder;
(function (EDeinterlaceFieldOrder) {
    EDeinterlaceFieldOrder[EDeinterlaceFieldOrder["Top"] = 0] = "Top";
    EDeinterlaceFieldOrder[EDeinterlaceFieldOrder["Bottom"] = 1] = "Bottom";
})(EDeinterlaceFieldOrder || (exports.EDeinterlaceFieldOrder = EDeinterlaceFieldOrder = {}));
var EVideoCodes;
(function (EVideoCodes) {
    EVideoCodes[EVideoCodes["Success"] = 0] = "Success";
    EVideoCodes[EVideoCodes["Fail"] = -1] = "Fail";
    EVideoCodes[EVideoCodes["NotSupported"] = -2] = "NotSupported";
    EVideoCodes[EVideoCodes["InvalidParam"] = -3] = "InvalidParam";
    EVideoCodes[EVideoCodes["CurrentlyActive"] = -4] = "CurrentlyActive";
    EVideoCodes[EVideoCodes["ModuleNotFound"] = -5] = "ModuleNotFound";
})(EVideoCodes || (exports.EVideoCodes = EVideoCodes = {}));
var EHotkeyObjectType;
(function (EHotkeyObjectType) {
    EHotkeyObjectType[EHotkeyObjectType["Frontend"] = 0] = "Frontend";
    EHotkeyObjectType[EHotkeyObjectType["Source"] = 1] = "Source";
    EHotkeyObjectType[EHotkeyObjectType["Output"] = 2] = "Output";
    EHotkeyObjectType[EHotkeyObjectType["Encoder"] = 3] = "Encoder";
    EHotkeyObjectType[EHotkeyObjectType["Service"] = 4] = "Service";
})(EHotkeyObjectType || (exports.EHotkeyObjectType = EHotkeyObjectType = {}));
var EDeinterlaceMode;
(function (EDeinterlaceMode) {
    EDeinterlaceMode[EDeinterlaceMode["Disable"] = 0] = "Disable";
    EDeinterlaceMode[EDeinterlaceMode["Discard"] = 1] = "Discard";
    EDeinterlaceMode[EDeinterlaceMode["Retro"] = 2] = "Retro";
    EDeinterlaceMode[EDeinterlaceMode["Blend"] = 3] = "Blend";
    EDeinterlaceMode[EDeinterlaceMode["Blend2X"] = 4] = "Blend2X";
    EDeinterlaceMode[EDeinterlaceMode["Linear"] = 5] = "Linear";
    EDeinterlaceMode[EDeinterlaceMode["Linear2X"] = 6] = "Linear2X";
    EDeinterlaceMode[EDeinterlaceMode["Yadif"] = 7] = "Yadif";
    EDeinterlaceMode[EDeinterlaceMode["Yadif2X"] = 8] = "Yadif2X";
})(EDeinterlaceMode || (exports.EDeinterlaceMode = EDeinterlaceMode = {}));
var EBlendingMethod;
(function (EBlendingMethod) {
    EBlendingMethod[EBlendingMethod["Default"] = 0] = "Default";
    EBlendingMethod[EBlendingMethod["SrgbOff"] = 1] = "SrgbOff";
})(EBlendingMethod || (exports.EBlendingMethod = EBlendingMethod = {}));
var EBlendingMode;
(function (EBlendingMode) {
    EBlendingMode[EBlendingMode["Normal"] = 0] = "Normal";
    EBlendingMode[EBlendingMode["Additive"] = 1] = "Additive";
    EBlendingMode[EBlendingMode["Substract"] = 2] = "Substract";
    EBlendingMode[EBlendingMode["Screen"] = 3] = "Screen";
    EBlendingMode[EBlendingMode["Multiply"] = 4] = "Multiply";
    EBlendingMode[EBlendingMode["Lighten"] = 5] = "Lighten";
    EBlendingMode[EBlendingMode["Darken"] = 6] = "Darken";
})(EBlendingMode || (exports.EBlendingMode = EBlendingMode = {}));
var EFontStyle;
(function (EFontStyle) {
    EFontStyle[EFontStyle["Bold"] = 1] = "Bold";
    EFontStyle[EFontStyle["Italic"] = 2] = "Italic";
    EFontStyle[EFontStyle["Underline"] = 4] = "Underline";
    EFontStyle[EFontStyle["Strikeout"] = 8] = "Strikeout";
})(EFontStyle || (exports.EFontStyle = EFontStyle = {}));
var EPropertyType;
(function (EPropertyType) {
    EPropertyType[EPropertyType["Invalid"] = 0] = "Invalid";
    EPropertyType[EPropertyType["Boolean"] = 1] = "Boolean";
    EPropertyType[EPropertyType["Int"] = 2] = "Int";
    EPropertyType[EPropertyType["Float"] = 3] = "Float";
    EPropertyType[EPropertyType["Text"] = 4] = "Text";
    EPropertyType[EPropertyType["Path"] = 5] = "Path";
    EPropertyType[EPropertyType["List"] = 6] = "List";
    EPropertyType[EPropertyType["Color"] = 7] = "Color";
    EPropertyType[EPropertyType["Button"] = 8] = "Button";
    EPropertyType[EPropertyType["Font"] = 9] = "Font";
    EPropertyType[EPropertyType["EditableList"] = 10] = "EditableList";
    EPropertyType[EPropertyType["FrameRate"] = 11] = "FrameRate";
    EPropertyType[EPropertyType["Group"] = 12] = "Group";
    EPropertyType[EPropertyType["ColorAlpha"] = 13] = "ColorAlpha";
    EPropertyType[EPropertyType["Capture"] = 14] = "Capture";
})(EPropertyType || (exports.EPropertyType = EPropertyType = {}));
var EListFormat;
(function (EListFormat) {
    EListFormat[EListFormat["Invalid"] = 0] = "Invalid";
    EListFormat[EListFormat["Int"] = 1] = "Int";
    EListFormat[EListFormat["Float"] = 2] = "Float";
    EListFormat[EListFormat["String"] = 3] = "String";
})(EListFormat || (exports.EListFormat = EListFormat = {}));
var EEditableListType;
(function (EEditableListType) {
    EEditableListType[EEditableListType["Strings"] = 0] = "Strings";
    EEditableListType[EEditableListType["Files"] = 1] = "Files";
    EEditableListType[EEditableListType["FilesAndUrls"] = 2] = "FilesAndUrls";
})(EEditableListType || (exports.EEditableListType = EEditableListType = {}));
var EPathType;
(function (EPathType) {
    EPathType[EPathType["File"] = 0] = "File";
    EPathType[EPathType["FileSave"] = 1] = "FileSave";
    EPathType[EPathType["Directory"] = 2] = "Directory";
})(EPathType || (exports.EPathType = EPathType = {}));
var ETextType;
(function (ETextType) {
    ETextType[ETextType["Default"] = 0] = "Default";
    ETextType[ETextType["Password"] = 1] = "Password";
    ETextType[ETextType["Multiline"] = 2] = "Multiline";
    ETextType[ETextType["TextInfo"] = 3] = "TextInfo";
})(ETextType || (exports.ETextType = ETextType = {}));
var ETextInfoType;
(function (ETextInfoType) {
    ETextInfoType[ETextInfoType["Normal"] = 0] = "Normal";
    ETextInfoType[ETextInfoType["Warning"] = 1] = "Warning";
    ETextInfoType[ETextInfoType["Error"] = 2] = "Error";
})(ETextInfoType || (exports.ETextInfoType = ETextInfoType = {}));
var ENumberType;
(function (ENumberType) {
    ENumberType[ENumberType["Scroller"] = 0] = "Scroller";
    ENumberType[ENumberType["Slider"] = 1] = "Slider";
})(ENumberType || (exports.ENumberType = ENumberType = {}));
var EAlignment;
(function (EAlignment) {
    EAlignment[EAlignment["Center"] = 0] = "Center";
    EAlignment[EAlignment["Left"] = 1] = "Left";
    EAlignment[EAlignment["Right"] = 2] = "Right";
    EAlignment[EAlignment["Top"] = 4] = "Top";
    EAlignment[EAlignment["Bottom"] = 8] = "Bottom";
    EAlignment[EAlignment["TopLeft"] = 5] = "TopLeft";
    EAlignment[EAlignment["TopRight"] = 6] = "TopRight";
    EAlignment[EAlignment["BottomLeft"] = 9] = "BottomLeft";
    EAlignment[EAlignment["BottomRight"] = 10] = "BottomRight";
})(EAlignment || (exports.EAlignment = EAlignment = {}));
var EOutputFlags;
(function (EOutputFlags) {
    EOutputFlags[EOutputFlags["Video"] = 1] = "Video";
    EOutputFlags[EOutputFlags["Audio"] = 2] = "Audio";
    EOutputFlags[EOutputFlags["AV"] = 3] = "AV";
    EOutputFlags[EOutputFlags["Encoded"] = 4] = "Encoded";
    EOutputFlags[EOutputFlags["Service"] = 8] = "Service";
    EOutputFlags[EOutputFlags["MultiTrack"] = 16] = "MultiTrack";
})(EOutputFlags || (exports.EOutputFlags = EOutputFlags = {}));
var ESourceOutputFlags;
(function (ESourceOutputFlags) {
    ESourceOutputFlags[ESourceOutputFlags["Video"] = 1] = "Video";
    ESourceOutputFlags[ESourceOutputFlags["Audio"] = 2] = "Audio";
    ESourceOutputFlags[ESourceOutputFlags["Async"] = 4] = "Async";
    ESourceOutputFlags[ESourceOutputFlags["AsyncVideo"] = 5] = "AsyncVideo";
    ESourceOutputFlags[ESourceOutputFlags["CustomDraw"] = 8] = "CustomDraw";
    ESourceOutputFlags[ESourceOutputFlags["Interaction"] = 32] = "Interaction";
    ESourceOutputFlags[ESourceOutputFlags["Composite"] = 64] = "Composite";
    ESourceOutputFlags[ESourceOutputFlags["DoNotDuplicate"] = 128] = "DoNotDuplicate";
    ESourceOutputFlags[ESourceOutputFlags["Deprecated"] = 256] = "Deprecated";
    ESourceOutputFlags[ESourceOutputFlags["DoNotSelfMonitor"] = 512] = "DoNotSelfMonitor";
    ESourceOutputFlags[ESourceOutputFlags["ForceUiRefresh"] = 1073741824] = "ForceUiRefresh";
})(ESourceOutputFlags || (exports.ESourceOutputFlags = ESourceOutputFlags = {}));
var ESceneDupType;
(function (ESceneDupType) {
    ESceneDupType[ESceneDupType["Refs"] = 0] = "Refs";
    ESceneDupType[ESceneDupType["Copy"] = 1] = "Copy";
    ESceneDupType[ESceneDupType["PrivateRefs"] = 2] = "PrivateRefs";
    ESceneDupType[ESceneDupType["PrivateCopy"] = 3] = "PrivateCopy";
})(ESceneDupType || (exports.ESceneDupType = ESceneDupType = {}));
var ESourceType;
(function (ESourceType) {
    ESourceType[ESourceType["Input"] = 0] = "Input";
    ESourceType[ESourceType["Filter"] = 1] = "Filter";
    ESourceType[ESourceType["Transition"] = 2] = "Transition";
    ESourceType[ESourceType["Scene"] = 3] = "Scene";
})(ESourceType || (exports.ESourceType = ESourceType = {}));
var EFaderType;
(function (EFaderType) {
    EFaderType[EFaderType["Cubic"] = 0] = "Cubic";
    EFaderType[EFaderType["IEC"] = 1] = "IEC";
    EFaderType[EFaderType["Log"] = 2] = "Log";
})(EFaderType || (exports.EFaderType = EFaderType = {}));
var EColorFormat;
(function (EColorFormat) {
    EColorFormat[EColorFormat["Unknown"] = 0] = "Unknown";
    EColorFormat[EColorFormat["A8"] = 1] = "A8";
    EColorFormat[EColorFormat["R8"] = 2] = "R8";
    EColorFormat[EColorFormat["RGBA"] = 3] = "RGBA";
    EColorFormat[EColorFormat["BGRX"] = 4] = "BGRX";
    EColorFormat[EColorFormat["BGRA"] = 5] = "BGRA";
    EColorFormat[EColorFormat["R10G10B10A2"] = 6] = "R10G10B10A2";
    EColorFormat[EColorFormat["RGBA16"] = 7] = "RGBA16";
    EColorFormat[EColorFormat["R16"] = 8] = "R16";
    EColorFormat[EColorFormat["RGBA16F"] = 9] = "RGBA16F";
    EColorFormat[EColorFormat["RGBA32F"] = 10] = "RGBA32F";
    EColorFormat[EColorFormat["RG16F"] = 11] = "RG16F";
    EColorFormat[EColorFormat["RG32F"] = 12] = "RG32F";
    EColorFormat[EColorFormat["R16F"] = 13] = "R16F";
    EColorFormat[EColorFormat["R32F"] = 14] = "R32F";
    EColorFormat[EColorFormat["DXT1"] = 15] = "DXT1";
    EColorFormat[EColorFormat["DXT3"] = 16] = "DXT3";
    EColorFormat[EColorFormat["DXT5"] = 17] = "DXT5";
})(EColorFormat || (exports.EColorFormat = EColorFormat = {}));
var EScaleType;
(function (EScaleType) {
    EScaleType[EScaleType["Disable"] = 0] = "Disable";
    EScaleType[EScaleType["Point"] = 1] = "Point";
    EScaleType[EScaleType["Bicubic"] = 2] = "Bicubic";
    EScaleType[EScaleType["Bilinear"] = 3] = "Bilinear";
    EScaleType[EScaleType["Lanczos"] = 4] = "Lanczos";
    EScaleType[EScaleType["Area"] = 5] = "Area";
})(EScaleType || (exports.EScaleType = EScaleType = {}));
var EFPSType;
(function (EFPSType) {
    EFPSType[EFPSType["Common"] = 0] = "Common";
    EFPSType[EFPSType["Integer"] = 1] = "Integer";
    EFPSType[EFPSType["Fractional"] = 2] = "Fractional";
})(EFPSType || (exports.EFPSType = EFPSType = {}));
var ERangeType;
(function (ERangeType) {
    ERangeType[ERangeType["Default"] = 0] = "Default";
    ERangeType[ERangeType["Partial"] = 1] = "Partial";
    ERangeType[ERangeType["Full"] = 2] = "Full";
})(ERangeType || (exports.ERangeType = ERangeType = {}));
var EVideoFormat;
(function (EVideoFormat) {
    EVideoFormat[EVideoFormat["None"] = 0] = "None";
    EVideoFormat[EVideoFormat["I420"] = 1] = "I420";
    EVideoFormat[EVideoFormat["NV12"] = 2] = "NV12";
    EVideoFormat[EVideoFormat["YVYU"] = 3] = "YVYU";
    EVideoFormat[EVideoFormat["YUY2"] = 4] = "YUY2";
    EVideoFormat[EVideoFormat["UYVY"] = 5] = "UYVY";
    EVideoFormat[EVideoFormat["RGBA"] = 6] = "RGBA";
    EVideoFormat[EVideoFormat["BGRA"] = 7] = "BGRA";
    EVideoFormat[EVideoFormat["BGRX"] = 8] = "BGRX";
    EVideoFormat[EVideoFormat["Y800"] = 9] = "Y800";
    EVideoFormat[EVideoFormat["I444"] = 10] = "I444";
    EVideoFormat[EVideoFormat["BGR3"] = 11] = "BGR3";
    EVideoFormat[EVideoFormat["I422"] = 12] = "I422";
    EVideoFormat[EVideoFormat["I40A"] = 13] = "I40A";
    EVideoFormat[EVideoFormat["I42A"] = 14] = "I42A";
    EVideoFormat[EVideoFormat["YUVA"] = 15] = "YUVA";
    EVideoFormat[EVideoFormat["AYUV"] = 16] = "AYUV";
})(EVideoFormat || (exports.EVideoFormat = EVideoFormat = {}));
var EBoundsType;
(function (EBoundsType) {
    EBoundsType[EBoundsType["None"] = 0] = "None";
    EBoundsType[EBoundsType["Stretch"] = 1] = "Stretch";
    EBoundsType[EBoundsType["ScaleInner"] = 2] = "ScaleInner";
    EBoundsType[EBoundsType["ScaleOuter"] = 3] = "ScaleOuter";
    EBoundsType[EBoundsType["ScaleToWidth"] = 4] = "ScaleToWidth";
    EBoundsType[EBoundsType["ScaleToHeight"] = 5] = "ScaleToHeight";
    EBoundsType[EBoundsType["MaxOnly"] = 6] = "MaxOnly";
})(EBoundsType || (exports.EBoundsType = EBoundsType = {}));
var EColorSpace;
(function (EColorSpace) {
    EColorSpace[EColorSpace["Default"] = 0] = "Default";
    EColorSpace[EColorSpace["CS601"] = 1] = "CS601";
    EColorSpace[EColorSpace["CS709"] = 2] = "CS709";
    EColorSpace[EColorSpace["CSSRGB"] = 3] = "CSSRGB";
    EColorSpace[EColorSpace["CS2100PQ"] = 4] = "CS2100PQ";
    EColorSpace[EColorSpace["CS2100HLG"] = 5] = "CS2100HLG";
})(EColorSpace || (exports.EColorSpace = EColorSpace = {}));
var ESpeakerLayout;
(function (ESpeakerLayout) {
    ESpeakerLayout[ESpeakerLayout["Unknown"] = 0] = "Unknown";
    ESpeakerLayout[ESpeakerLayout["Mono"] = 1] = "Mono";
    ESpeakerLayout[ESpeakerLayout["Stereo"] = 2] = "Stereo";
    ESpeakerLayout[ESpeakerLayout["TwoOne"] = 3] = "TwoOne";
    ESpeakerLayout[ESpeakerLayout["Four"] = 4] = "Four";
    ESpeakerLayout[ESpeakerLayout["FourOne"] = 5] = "FourOne";
    ESpeakerLayout[ESpeakerLayout["FiveOne"] = 6] = "FiveOne";
    ESpeakerLayout[ESpeakerLayout["SevenOne"] = 8] = "SevenOne";
})(ESpeakerLayout || (exports.ESpeakerLayout = ESpeakerLayout = {}));
var EOutputCode;
(function (EOutputCode) {
    EOutputCode[EOutputCode["Success"] = 0] = "Success";
    EOutputCode[EOutputCode["BadPath"] = -1] = "BadPath";
    EOutputCode[EOutputCode["ConnectFailed"] = -2] = "ConnectFailed";
    EOutputCode[EOutputCode["InvalidStream"] = -3] = "InvalidStream";
    EOutputCode[EOutputCode["Error"] = -4] = "Error";
    EOutputCode[EOutputCode["Disconnected"] = -5] = "Disconnected";
    EOutputCode[EOutputCode["Unsupported"] = -6] = "Unsupported";
    EOutputCode[EOutputCode["NoSpace"] = -7] = "NoSpace";
    EOutputCode[EOutputCode["EncoderError"] = -8] = "EncoderError";
    EOutputCode[EOutputCode["OutdatedDriver"] = -65] = "OutdatedDriver";
})(EOutputCode || (exports.EOutputCode = EOutputCode = {}));
var ECategoryTypes;
(function (ECategoryTypes) {
    ECategoryTypes[ECategoryTypes["NODEOBS_CATEGORY_LIST"] = 0] = "NODEOBS_CATEGORY_LIST";
    ECategoryTypes[ECategoryTypes["NODEOBS_CATEGORY_TAB"] = 1] = "NODEOBS_CATEGORY_TAB";
})(ECategoryTypes || (exports.ECategoryTypes = ECategoryTypes = {}));
var ERenderingMode;
(function (ERenderingMode) {
    ERenderingMode[ERenderingMode["OBS_MAIN_RENDERING"] = 0] = "OBS_MAIN_RENDERING";
    ERenderingMode[ERenderingMode["OBS_STREAMING_RENDERING"] = 1] = "OBS_STREAMING_RENDERING";
    ERenderingMode[ERenderingMode["OBS_RECORDING_RENDERING"] = 2] = "OBS_RECORDING_RENDERING";
})(ERenderingMode || (exports.ERenderingMode = ERenderingMode = {}));
var EIPCError;
(function (EIPCError) {
    EIPCError[EIPCError["STILL_RUNNING"] = 259] = "STILL_RUNNING";
    EIPCError[EIPCError["VERSION_MISMATCH"] = 252] = "VERSION_MISMATCH";
    EIPCError[EIPCError["OTHER_ERROR"] = 253] = "OTHER_ERROR";
    EIPCError[EIPCError["MISSING_DEPENDENCY"] = 254] = "MISSING_DEPENDENCY";
    EIPCError[EIPCError["NORMAL_EXIT"] = 0] = "NORMAL_EXIT";
})(EIPCError || (exports.EIPCError = EIPCError = {}));
var EVcamInstalledStatus;
(function (EVcamInstalledStatus) {
    EVcamInstalledStatus[EVcamInstalledStatus["NotInstalled"] = 0] = "NotInstalled";
    EVcamInstalledStatus[EVcamInstalledStatus["LegacyInstalled"] = 1] = "LegacyInstalled";
    EVcamInstalledStatus[EVcamInstalledStatus["Installed"] = 2] = "Installed";
})(EVcamInstalledStatus || (exports.EVcamInstalledStatus = EVcamInstalledStatus = {}));
var ERecSplitType;
(function (ERecSplitType) {
    ERecSplitType[ERecSplitType["Time"] = 0] = "Time";
    ERecSplitType[ERecSplitType["Size"] = 1] = "Size";
    ERecSplitType[ERecSplitType["Manual"] = 2] = "Manual";
})(ERecSplitType || (exports.ERecSplitType = ERecSplitType = {}));
exports.Global = obs.Global;
exports.Video = obs.Video;
exports.VideoFactory = obs.Video;
exports.InputFactory = obs.Input;
exports.SceneFactory = obs.Scene;
exports.FilterFactory = obs.Filter;
exports.TransitionFactory = obs.Transition;
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
var EInteractionFlags;
(function (EInteractionFlags) {
    EInteractionFlags[EInteractionFlags["None"] = 0] = "None";
    EInteractionFlags[EInteractionFlags["CapsKey"] = 1] = "CapsKey";
    EInteractionFlags[EInteractionFlags["ShiftKey"] = 2] = "ShiftKey";
    EInteractionFlags[EInteractionFlags["ControlKey"] = 4] = "ControlKey";
    EInteractionFlags[EInteractionFlags["AltKey"] = 8] = "AltKey";
    EInteractionFlags[EInteractionFlags["MouseLeft"] = 16] = "MouseLeft";
    EInteractionFlags[EInteractionFlags["MouseMiddle"] = 32] = "MouseMiddle";
    EInteractionFlags[EInteractionFlags["MouseRight"] = 64] = "MouseRight";
    EInteractionFlags[EInteractionFlags["CommandKey"] = 128] = "CommandKey";
    EInteractionFlags[EInteractionFlags["Numlock_Key"] = 256] = "Numlock_Key";
    EInteractionFlags[EInteractionFlags["IsKeyPad"] = 512] = "IsKeyPad";
    EInteractionFlags[EInteractionFlags["IsLeft"] = 1024] = "IsLeft";
    EInteractionFlags[EInteractionFlags["IsRight"] = 2048] = "IsRight";
})(EInteractionFlags || (exports.EInteractionFlags = EInteractionFlags = {}));
;
var EMouseButtonType;
(function (EMouseButtonType) {
    EMouseButtonType[EMouseButtonType["Left"] = 0] = "Left";
    EMouseButtonType[EMouseButtonType["Middle"] = 1] = "Middle";
    EMouseButtonType[EMouseButtonType["Right"] = 2] = "Right";
})(EMouseButtonType || (exports.EMouseButtonType = EMouseButtonType = {}));
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
var ERecordingFormat;
(function (ERecordingFormat) {
    ERecordingFormat["MP4"] = "mp4";
    ERecordingFormat["FLV"] = "flv";
    ERecordingFormat["MOV"] = "mov";
    ERecordingFormat["MKV"] = "mkv";
    ERecordingFormat["MPEGTS"] = "ts";
    ERecordingFormat["HLS"] = "m3u8";
})(ERecordingFormat || (exports.ERecordingFormat = ERecordingFormat = {}));
var ERecordingQuality;
(function (ERecordingQuality) {
    ERecordingQuality[ERecordingQuality["Stream"] = 0] = "Stream";
    ERecordingQuality[ERecordingQuality["HighQuality"] = 1] = "HighQuality";
    ERecordingQuality[ERecordingQuality["HigherQuality"] = 2] = "HigherQuality";
    ERecordingQuality[ERecordingQuality["Lossless"] = 3] = "Lossless";
})(ERecordingQuality || (exports.ERecordingQuality = ERecordingQuality = {}));
var EVideoEncoderType;
(function (EVideoEncoderType) {
    EVideoEncoderType[EVideoEncoderType["Audio"] = 0] = "Audio";
    EVideoEncoderType[EVideoEncoderType["Video"] = 1] = "Video";
})(EVideoEncoderType || (exports.EVideoEncoderType = EVideoEncoderType = {}));
var EProcessPriority;
(function (EProcessPriority) {
    EProcessPriority["High"] = "High";
    EProcessPriority["AboveNormal"] = "AboveNormal";
    EProcessPriority["Normal"] = "Normal";
    EProcessPriority["BelowNormal"] = "BelowNormal";
    EProcessPriority["Idle"] = "Idle";
})(EProcessPriority || (exports.EProcessPriority = EProcessPriority = {}));
var VCamOutputType;
(function (VCamOutputType) {
    VCamOutputType[VCamOutputType["Invalid"] = 0] = "Invalid";
    VCamOutputType[VCamOutputType["SceneOutput"] = 1] = "SceneOutput";
    VCamOutputType[VCamOutputType["SourceOutput"] = 2] = "SourceOutput";
    VCamOutputType[VCamOutputType["ProgramView"] = 3] = "ProgramView";
    VCamOutputType[VCamOutputType["PreviewOutput"] = 4] = "PreviewOutput";
})(VCamOutputType || (exports.VCamOutputType = VCamOutputType = {}));
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
