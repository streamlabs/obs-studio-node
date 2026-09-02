import * as path from 'path';
import * as fs from 'fs';
// Mac- search for optional OSN.app bundle (Chromium requires an app bundle to find obs64 helper apps)
const hasDeveloperApp = process.platform === 'darwin' && fs.existsSync(path.join(__dirname, 'OSN.app'));
const obs = hasDeveloperApp
  ? require('./OSN.app/distribute/obs-studio-node/obs_studio_client.node')
  : require('./obs_studio_client.node');

/* Convenient paths to modules */
export const DefaultD3D11Path: string =
    path.resolve(__dirname, `libobs-d3d11.dll`);

export const DefaultOpenGLPath: string =
    path.resolve(__dirname, `libobs-opengl.dll`);

export const DefaultDrawPluginPath: string =
    path.resolve(__dirname, `simple_draw.dll`);

export const DefaultBinPath: string =
    path.resolve(__dirname);

export const DefaultDataPath: string =
    path.resolve(__dirname, `data`);

export const DefaultPluginPath: string =
    path.resolve(__dirname, `obs-plugins`);

export const DefaultPluginDataPath: string =
    path.resolve(__dirname, `data/obs-plugins/%module%`);

export const DefaultPluginPathMac: string =
    path.resolve(__dirname, `PlugIns`);

/**
 * To be passed to Input.flags
 */
export const enum ESourceFlags {
    Unbuffered = (1 << 0),
    ForceMono = (1 << 1)
}

export const enum EMonitoringType {
    None,
    MonitoringOnly,
    MonitoringAndOutput
}

export const enum EOrderMovement {
    Up,
    Down,
    Top,
    Bottom
}

export const enum EDeinterlaceFieldOrder {
    Top,
    Bottom
}

export const enum EVideoCodes {
	Success = 0,
	Fail = -1,
	NotSupported = -2,
	InvalidParam = -3,
	CurrentlyActive = -4,
	ModuleNotFound = -5
}

export const enum EHotkeyObjectType {
	Frontend = 0,
	Source = 1,
	Output = 2,
	Encoder = 3,
	Service = 4
}

export const enum EDeinterlaceMode {
    Disable,
    Discard,
    Retro,
    Blend,
    Blend2X,
    Linear,
    Linear2X,
    Yadif,
    Yadif2X
}

export const enum EBlendingMethod {
    Default,
    SrgbOff
}

export const enum EBlendingMode {
    Normal,
    Additive,
    Substract,
    Screen,
    Multiply,
    Lighten,
    Darken
}

export const enum EFontStyle {
  Bold = (1<<0),
  Italic = (1<<1),
  Underline = (1<<2),
  Strikeout = (1<<3),
}

/**
 * Enumeration describing the type of a property
 */
export const enum EPropertyType {
    Invalid,
    Boolean,
    Int,
    Float,
    Text,
    Path,
    List,
    Color,
    Button,
    Font,
    EditableList,
    FrameRate,
    Group,
    ColorAlpha,
    Capture,
}

export const enum EListFormat {
    Invalid,
    Int,
    Float,
    String
}

export const enum EEditableListType {
    Strings,
    Files,
    FilesAndUrls
}

export const enum EPathType {
    File,
    FileSave,
    Directory
}

export const enum ETextType {
    Default,
    Password,
    Multiline,
    TextInfo
}

export const enum ETextInfoType {
	Normal,
	Warning,
	Error,
}

export const enum ENumberType {
    Scroller,
    Slider
}

/**
 * A binary flag representing alignment
 */
export const enum EAlignment {
    Center = 0,
    Left = (1 << 0),
    Right = (1 << 1),
    Top = (1 << 2),
    Bottom = (1 << 3),
    TopLeft = (Top | Left),
    TopRight = (Top | Right),
    BottomLeft = (Bottom | Left),
    BottomRight = (Bottom | Right)
}

/**
 * A binary flag representing output capabilities
 * Apparently you can't fetch these for now (???)
 */
export const enum EOutputFlags {
    Video = (1<<0),
    Audio = (1<<1),
    AV = (Video | Audio),
    Encoded = (1<<2),
    Service = (1<<3),
    MultiTrack = (1<<4)
}

/**
 * A binary flag representing source output capabilities
 */
export const enum ESourceOutputFlags {
    Video = (1 << 0),
    Audio = (1 << 1),
    Async = (1 << 2),
    AsyncVideo = Async | Video,
    CustomDraw = (1 << 3),
    Interaction = (1 << 5),
    Composite = (1 << 6),
    DoNotDuplicate = (1 << 7),
    Deprecated = (1 << 8),
    DoNotSelfMonitor = (1 << 9),
    // The flag below is a Stremlabs extension to force UI refresh on source properties update
    ForceUiRefresh = (1 << 30),
}

export const enum ESceneDupType {
    Refs,
    Copy,
    PrivateRefs,
    PrivateCopy
}

export interface IOBSAPIInitializationOptions {
    /** OBS locale. */
    language: string;

    /** Application data directory. */
    appDataPath: string;

    /** Application version. */
    version: string;

    /**
     * Crash-reporting server URL override.
     * Pass an empty string to use the built-in endpoint for the current release channel.
     */
    crashServerUrl: string;
}

/**
 * Describes the type of source
 */
export const enum ESourceType {
    Input,
    Filter,
    Transition,
    Scene,
}

/**
 * Describes algorithm type to use for volume representation.
 */
export const enum EFaderType {
    Cubic,
    IEC /* IEC 60-268-18 */,
    Log /* Logarithmic */
}

export const enum EColorFormat {
	Unknown,
	A8,
	R8,
	RGBA,
	BGRX,
	BGRA,
	R10G10B10A2,
	RGBA16,
	R16,
	RGBA16F,
	RGBA32F,
	RG16F,
	RG32F,
	R16F,
	R32F,
	DXT1,
	DXT3,
	DXT5
}

export const enum EScaleType {
    Disable,
    Point,
    Bicubic,
    Bilinear,
    Lanczos,
    Area
}

export const enum EFPSType {
    Common,
    Integer,
    Fractional
}

export const enum ERangeType {
    Default,
    Partial,
    Full
}

export const enum EVideoFormat {
    None,
    I420,
    NV12,
    YVYU,
    YUY2,
    UYVY,
    RGBA,
    BGRA,
    BGRX,
    Y800,
    I444,
    BGR3,
    I422,
    I40A,
    I42A,
    YUVA,
    AYUV
}

export const enum EBoundsType {
    None,
    Stretch,
    ScaleInner,
    ScaleOuter,
    ScaleToWidth,
    ScaleToHeight,
    MaxOnly
}

export const enum EColorSpace {
    Default,
    CS601,
    CS709,
    CSSRGB,
    CS2100PQ,
    CS2100HLG
}

export const enum ESpeakerLayout {
    Unknown,
    Mono,
    Stereo,
    TwoOne,
    Four,
    FourOne,
    FiveOne,
    SevenOne = 8
}

export const enum EOutputCode {
    Success = 0,
    BadPath = -1,
    ConnectFailed = -2,
    InvalidStream = -3,
    Error = -4,
    Disconnected = -5,
    Unsupported = -6,
    NoSpace = -7,
    EncoderError = -8,
    OutdatedDriver = -65,
}

export const enum ECategoryTypes {
    NODEOBS_CATEGORY_LIST = 0,
	NODEOBS_CATEGORY_TAB = 1
}

export const enum ERenderingMode {
    OBS_MAIN_RENDERING = 0,
	OBS_STREAMING_RENDERING = 1,
	OBS_RECORDING_RENDERING = 2
}

export const enum EIPCError {
    STILL_RUNNING = 259,
    VERSION_MISMATCH = 252,
    OTHER_ERROR = 253,
    MISSING_DEPENDENCY = 254,
    NORMAL_EXIT = 0,
}

export const enum EVcamInstalledStatus {
    NotInstalled = 0,
    LegacyInstalled = 1,
    Installed = 2
}

export const enum ERecSplitType {
    Time = 0,
    Size = 1,
    Manual = 2
}

export const Global: IGlobal = obs.Global;
export const Video: IVideo = obs.Video;
export const VideoFactory: IVideoFactory = obs.Video;
export const InputFactory: IInputFactory = obs.Input;
export const SceneFactory: ISceneFactory = obs.Scene;
export const FilterFactory: IFilterFactory = obs.Filter;
export const TransitionFactory: ITransitionFactory = obs.Transition;
export const VolmeterFactory: IVolmeterFactory = obs.Volmeter;
export const FaderFactory: IFaderFactory = obs.Fader;
export const Audio: IAudio = obs.Audio;
export const AudioFactory: IAudioFactory = obs.Audio;
export const ModuleFactory: IModuleFactory = obs.Module;
export const IPC: IIPC = obs.IPC;
export const VideoEncoderFactory: IVideoEncoderFactory = obs.VideoEncoder;
export const ServiceFactory: IServiceFactory = obs.Service;
export const SimpleStreamingFactory: ISimpleStreamingFactory = obs.SimpleStreaming;
export const AdvancedStreamingFactory: IAdvancedStreamingFactory = obs.AdvancedStreaming;
export const EnhancedBroadcastingAdvancedStreamingFactory: IEnhancedBroadcastingAdvancedStreamingFactory = obs.EnhancedBroadcastingAdvancedStreaming;
export const EnhancedBroadcastingSimpleStreamingFactory: IEnhancedBroadcastingSimpleStreamingFactory = obs.EnhancedBroadcastingSimpleStreaming;
export const DelayFactory: IDelayFactory = obs.Delay;
export const ReconnectFactory: IReconnectFactory = obs.Reconnect;
export const NetworkFactory: INetworkFactory = obs.Network;
export const AudioTrackFactory: IAudioTrackFactory = obs.AudioTrack;
export const SimpleRecordingFactory: ISimpleRecordingFactory = obs.SimpleRecording;
export const AdvancedRecordingFactory: IAdvancedRecordingFactory = obs.AdvancedRecording;
export const AudioEncoderFactory: IAudioEncoderFactory = obs.AudioEncoder;
export const SimpleReplayBufferFactory: ISimpleReplayBufferFactory = obs.SimpleReplayBuffer;
export const AdvancedReplayBufferFactory: IAdvancedReplayBufferFactory = obs.AdvancedReplayBuffer;

/**
 * Meta object in order to better describe settings
 */
export interface ISettings {
    [key: string]: any;
}

/**
 * Used for various 2-dimensional functions
 */
export interface IVec2 {
    readonly x: number;
    readonly y: number;
}

/**
 * Used to represented a time in nanoseconds
 * JS can't hold 64-bit integers thus can
 * easily overflow when representing time in ns.
 */
export interface ITimeSpec {
    readonly sec: number;
    readonly nsec: number;
}

/**
 * Interface describing the transform information in an item
 */
export interface ITransformInfo {
    readonly pos: IVec2;
    readonly rot: number;
    readonly scale: IVec2;
    readonly alignment: EAlignment;
    readonly boundsType: EBoundsType;
    readonly boundsAlignment: number;
    readonly bounds: IVec2;
    readonly cropToBounds: boolean;
}

/**
 * Interface describing the crop of an item.
 */
export interface ICropInfo {
    readonly left: number;
    readonly right: number;
    readonly top: number;
    readonly bottom: number;
    /** Canvas dimensions against which a nested-scene crop was authored. */
    readonly referenceWidth?: number;
    readonly referenceHeight?: number;
}

/**
 * Namespace representing the global libobs functionality
 */

export interface IIPC {
    /**
     * Set the path and optionally working directory for the IPC server binary.
     * @param binaryPath - Path to the binary file to be executed
     * @param workingDirectoryPath - Path to the directory where it is executed in.
	 * @throws SyntaxError if an invalid number of parameters is given.
	 * @throws TypeError if a parameter is of invalid type.
     */
	setServerPath(binaryPath: string, workingDirectoryPath?: string): void;

    /**
     * Connect to an existing server.
     * @param uri - URI for the server.
	 * @throws SyntaxError if an invalid number of parameters is given.
	 * @throws TypeError if a parameter is of invalid type.
	 * @throws Error if it failed to connect.
     */
	connect(uri: string): void;

    /**
     * Hosts a new server and connects to it.
     * @param uri - URI for the server.
	 * @throws SyntaxError if an invalid number of parameters is given.
	 * @throws TypeError if a parameter is of invalid type.
	 * @throws Error if it failed to host and connect.
     */
	host(uri: string): EIPCError;

    /**
     * Disconnect from a server.
     */
	disconnect(): void;
}

export interface IGlobal {
    /**
     * @param id - String ID of the source
     * @returns - The output flags (capabilities) of the source type
     */
    getOutputFlagsFromId(id: string): number;

    /**
     * Output channels are useful in that we can attach multiple
     * sources for output. For the most part, you're generally only
     * going to use one channel for video. However, if you so wanted,
     * you could assign more to be layered on top of other channels.
     *
     * This also accepts audio input sources which are automatically
     * mixed into the audio output. This means you can have a standalone
     * input source that isn't attached to the scene being rendered.
     * @param channel - The output channel to assign source
     * @param input - The source to assign to the output channel
     */
    setOutputSource(channel: number, input: ISource): void;

    /**
     * Obtains the source associated with a given output channel
     * @param channel - The output channel to fetch source of
     * @returns - The associated source or null if none was assigned to the given channel or channel was invalid.
     */
    getOutputSource(channel: number): ISource;

    /**
     * Adds scene to backstage. This action allow to keep it active
     * and not display on stream or recording.
     *
     * This is used to create scene previews mostly.
     *
     * @param input - The scene source
     */
    addSceneToBackstage(input: ISource) : void;

    /**
     * Removes scene from backstage and cleans up resources if needed.
     *
     * @param input - The scene source
     */
    removeSceneFromBackstage(input: ISource): void;

    /**
     * Number of total render frames
     */
    readonly totalFrames: number;

    /**
     * Number of total lost frames due to being short
     * of rendering time.
     */
    readonly laggedFrames: number;

    /**
     * Current locale of current libobs context
     */
    locale: string;

    /**
     * Rendering of current libobs context
     */
    multipleRendering: boolean;

    /**
     * Percentage of CPU being used
     */
    readonly cpuPercentage: number;

    /**
     * Current FPS
     */
    readonly currentFrameRate: number;

    /**
     * Average time to render a frame
     */
    readonly averageFrameRenderTime: number;

    /**
     * Disk space currentlky available
     */
    readonly diskSpaceAvailable: number;

    /**
     * Current memory usage
     */
    readonly memoryUsage: number;
}

export interface IBooleanProperty extends IProperty {

}

export interface IColorProperty extends IProperty {

}
export interface ICaptureProperty extends IProperty {

}

export interface IButtonProperty extends IProperty {
    /**
     * @param source An object containing context
     * used by the plugin. This is always the source
     * associated with the property. Right now, I
     * just accept a generic object for forward
     * compatibility.
     */
    buttonClicked(source: object): void;
}

export interface IFontProperty extends IProperty {

}

export interface IListProperty extends IProperty {
    readonly details: IListDetails;
}

export interface IListDetails {
    readonly format: EListFormat;

    /**
     * A list of options to be made available within the list.
     * You can determine if it's a string or number by testing
     * {@link IListProperty#format}
     */
    readonly items: { name: string, value: string | number }[];
}

export interface IEditableListProperty extends IProperty {
    readonly details: IEditableListDetails;
}

export interface IEditableListDetails extends IListDetails {
    readonly type: EEditableListType;

     /** String describing allowed valued */
    readonly filter: string;

    /** Default value for the editable box */
    readonly defaultPath: string;
}

export interface IPathProperty extends IProperty {
    readonly details: IPathDetails;
}

export interface IPathDetails {
    readonly type: EPathType;
    readonly filter: string;
    readonly defaultPath: string;
}

export interface ITextProperty extends IProperty {
    readonly details: ITextDetails;
}

export interface ITextDetails {
    readonly type: ETextType;
    readonly infoType: ETextInfoType;
}

export interface INumberProperty extends IProperty {
    readonly details: INumberDetails;
}

export interface INumberDetails {
    readonly type: ENumberType;
    readonly min: number;
    readonly max: number;
    readonly step: number;
}

/**
 * Class representing an entry in a properties list (Properties).
 */
export interface IProperty {
    /**
     * The name associated with the property
     * You can use this name to fetch from the source
     * settings object (see {@link ObsSource#settings})
     * if you need the value set on this property.
     */
    readonly name: string;

    /** A short description of the property */
    readonly description: string;

    /** A long detailed description of the property */
    readonly longDescription: string;

    /** Whether or not the property is enabled */
    readonly enabled: boolean;

    /** Whether or not the property should be made visible */
    readonly visible: boolean;

    /** Type of the property */
    readonly type: EPropertyType;

    /** Current value of the property */
    readonly value: any;

    /**
     * Uses the current object to obtain the next
     * property in the properties list.
     *
     * @returns If it's successful, returns true.
     * Otherwise or if end of the list, returns false.
     */
    next(): IProperty;

    /**
     * Uses the current object to obtain the previous property in the list.
     * Returns undefined when the current property is the first.
     */
    previous(): IProperty;

    /** True when this property is the first in the list. */
    is_first(): boolean;

    /** True when this property is the last in the list. */
    is_last(): boolean;

    modified(): boolean;
}

/**
 * Object representing a list of properties.
 *
 * Use .properties member on an encoder, source, output, or service
 * to obtain an instance.
 */
export interface IProperties {

    /** Obtains the first property in the list. */
    first(): IProperty;

    /** Obtains the last property in the list. */
    last(): IProperty;

    count(): number;

    /**
     * Obtains property matching name.
     * @param name The name of the property to fetch.
     * @returns - The property instance or null if not found
     */
    get(name: string): IProperty;
}

export interface IFactoryTypes {
    types(): string[];
}

export interface IReleasable {
    release(): void;
}

export interface IFilterFactory extends IFactoryTypes {
    /**
     * Create an instance of an ObsFilter
     * @param id - ID of the filter, possibly returned from types()
     * @param name - Name of the filter
     * @param settings - Optional, settings to create the filter with
     * @returns - Created instance of ObsFilter or null if failure
     */
    create(id: string, name: string, settings?: ISettings): IFilter;
}

/**
 * Class representing a filter
 */
export interface IFilter extends ISource {
}

export interface IInputFactory extends IFactoryTypes {
    /**
     * Create a new instance of an ObsInput
     * @param id - The type of input source to create, possibly from {@link types}
     * @param name - Name of the created input source
     * @param settings - Optional, settings to create input sourc with
     * @param hotkeys - Optional, hotkey data associated with input
     * @returns - Returns instance or null if failure
     */
    create(id: string, name: string, settings?: ISettings, hotkeys?: ISettings): IInput;

    /**
     * Create a new instance of an ObsInput that's private
     * Private in this context means any function that returns an
     * ObsInput will not return this source
     * @param id - The type of input source to create, possibly from {@link types}
     * @param name - Name of the created input source
     * @param settings - Optional, settings to create input source with
     * @returns - Returns instance or null if failure
     */
    createPrivate(id: string, name: string, settings?: ISettings): IInput;

    /**
     * Create an instance of an ObsInput by fetching the source by name.
     * @param name - Name of the source to look for
     * @returns - Returns instance or null if it failed to find the source
     */
    fromName(name: string): IInput;

    /**
     * Fetches a list of all public input sources available.
     */
    getPublicSources(): IInput[];
}


export const enum EInteractionFlags {
	None         = 0,
	CapsKey      = 1,
	ShiftKey     = 1 << 1,
	ControlKey   = 1 << 2,
	AltKey       = 1 << 3,
	MouseLeft    = 1 << 4,
	MouseMiddle  = 1 << 5,
	MouseRight   = 1 << 6,
	CommandKey   = 1 << 7,
	Numlock_Key  = 1 << 8,
	IsKeyPad     = 1 << 9,
	IsLeft       = 1 << 10,
	IsRight      = 1 << 11
};

export const enum EMouseButtonType {
	Left,
	Middle,
	Right
};

export interface IMouseEvent {
	modifiers: EInteractionFlags;
	x: number;
	y: number;
};

export interface IKeyEvent {
	modifiers: EInteractionFlags;
	text: string;
	nativeModifiers: number;
	nativeScancode: number;
	nativeVkey: number;
};

export interface ISceneItemInfo {
    name: string,
    crop: ICropInfo,
    scaleX: number,
    scaleY: number,
    visible: boolean,
    x: number,
    y: number,
    rotation: number
    streamVisible: boolean,
    recordingVisible: boolean,
    scaleFilter: EScaleType,
    blendingMode: EBlendingMode,
    blendingMethod: EBlendingMethod
}

/**
 * Class representing a source
 *
 * An input source can be either an audio or video or even both.
 * So some of these don't make sense right now. For instance, there's
 * no reason tot call volume on a source that only provides video input.
 */
export interface IInput extends ISource {
    volume: number;
    syncOffset: ITimeSpec;
    showing: boolean;
    audioMixers: number;
    monitoringType: EMonitoringType;
    deinterlaceFieldOrder: EDeinterlaceFieldOrder;
    deinterlaceMode: EDeinterlaceMode;

    /**
     * Create a new instance using the current instance.
     * If no parameters are provide, an instance is created
     * using the current instance as if it were new.
     * @param name - Name of new source
     * @param isPrivate - Whether or not the new source is private
     */
    duplicate(name?: string, isPrivate?: boolean): IInput;

    /**
     * Find a filter associated with the input source by name.
     * @param name - Name of filter to find
     * @returns - Returns the filter instance or null if it couldn't find the filter
     */
    findFilter(name: string): IFilter;

    /**
     * Attach a filter instance to this input source
     * @param filter - The filter instance to attach to this input source.
     */
    addFilter(filter: IFilter): void;

    /**
     * Remove a filter instance from this input source
     * @param filter - The filter instance to remove from this input source.
     */
    removeFilter(filter: IFilter): void;

    sendMouseClick(eventData: IMouseEvent, type: EMouseButtonType, mouseUp: boolean, clickCount: number): void
    sendMouseMove(eventData: IMouseEvent, mouseLeave: boolean): void;
    sendMouseWheel(eventData: IMouseEvent, x_delta: number, y_delta: number): void;
    sendFocus(focus: boolean): void;
    sendKeyClick(eventData: IKeyEvent, keyUp: boolean): void;

    /**
     * Move a filter up, down, top, or bottom in the filter list.
     * @param filter - The filter to move within the input source.
     * @param movement - The movement to make within the list.
     */
    setFilterOrder(filter: IFilter, movement: EOrderMovement): void;

    /**
     * Copy all filters from this input source onto another.
     * @param other - Destination input that will receive copies of this source's filters.
     * @returns - True on success, false if the IPC call failed.
     */
    copyFilters(other: IInput): boolean;

    /**
     * Obtain a list of all filters associated with the input source
     */
    readonly filters: IFilter[];

    /**
     * Whether the input is currently active (rendering / producing data).
     */
    readonly active: boolean;

    /**
     * Width of the underlying source
     */
    readonly width: number;

    /**
     * Height of the underlying source
     */
    readonly height: number;

    /**
     * get the duration of media file in milliseconds
     */
    getDuration(): number;

    /**
     * get or set the current play position
     */
    seek: number;

    /**
     * play media source
     */
    play(): void;

    /**
     * pause media source
     */
    pause(): void;

    /**
     * restart media source when ended
     */
    restart(): void;

    /**
     * stop media source
     */
    stop(): void;

    /**
     * Get the current media playback state of the source.
     */
    getMediaState(): number;

    /**
     * Re-trigger the source's load step (re-reads serialized state on the server).
     */
    load(): void;
}

export interface ISceneFactory {
    /**
     * Invalidates cached absolute scene-item transforms after a canvas reset.
     */
    invalidateItemTransformCache(): void;

    /**
     * Create a new scene instance
     * @param name - Name of the scene to create
     * @returns - Returns the instance or null on failure
     */
    create(name: string): IScene;

    /**
     * Create a new scene instance that's private
     * @param name - Name of the scene to create
     * @returns - Returns the instance or null on failure
     */
    createPrivate(name: string): IScene;

    /**
     * Create a new scene instance by fetching it by name
     * @param name - Name of the scene to look for
     * @returns - Returns the instance or null on failure to find the scene
     */
    fromName(name: string): IScene;
}

/**
 * Class representing a scene
 */
export interface IScene extends ISource {
    /**
     * Create a new instance of a scene using the current scene
     * @param name - New name of the duplicated scene
     * @param type - Method of scene item duplication
     */
    duplicate(name: string, type: ESceneDupType): IScene;

    /**
     * Add an input source to the scene, creating a scene item.
     * @param source - Input source to add to the scene
     * @param transform - Initial transform and visual settings for the scene item
     * @param video - Optional target video canvas, assigned before applying the transform. When omitted, the item remains
     * unassigned and renders on every canvas. Requires `transform` when provided.
     * @returns - The created scene item
     * @throws {TypeError} If `video` is provided without `transform` or is not an `IVideo` instance.
     * @throws {Error} If the scene, source, or supplied video canvas is no longer valid, or if OSN cannot create the item.
     * Reference validation failures do not add an item to the scene.
     */
    add(source: IInput, transform?: ISceneItemInfo, video?: IVideo): ISceneItem;

    /**
     * A scene may be used as an input source (even though its type
     * will still be of the value Scene). To use a scene as an ObsInput,
     * simply fetch this property.
     */
    readonly source: IInput;

    /**
     * Orders an item from the old index to the new index (from bottom to top)
     * @param oldIndex - Item index
     * @param newIndex - Index where you want to move item
     */
    moveItem(oldIndex: number, newIndex: number): void;

    /**
     * Orders an scene items as provided
     * @param order - Item ids in needed order
     */
    orderItems( order: number[] ): void;

    /**
     * Find an item within a scene
     *
     * @param id - A string representing the name of the
     * underlying source of the item to search for or an integer specifying
     * the id assigned to the item when it was created.
     *
     * @returns - The found item instance or null
     */
    findItem(id: string | number): ISceneItem;

    /**
     * Find an item within a scene by index
     *
     * @param idx - An integer representing the index the item sits at within the scene
     * @returns - The item instance or null if the index was bad
     */
    getItemAtIdx(idx: number): ISceneItem;

    /**
     * Fetch all items within the scene
     * @returns - The array of item instances
     */
    getItems(): ISceneItem[];

    /**
     * Fetch a contiguous range of items within the scene.
     * @param fromIndex - Inclusive start index.
     * @param toIndex - Inclusive end index.
     */
    getItemsInRange(fromIndex: number, toIndex: number): ISceneItem[];

    /**
     * Re-trigger the scene's load step on the server.
     */
    load(): void;

    sendMouseClick(eventData: IMouseEvent, type: EMouseButtonType, mouseUp: boolean, clickCount: number): void;
    sendMouseMove(eventData: IMouseEvent, mouseLeave: boolean): void;
    sendMouseWheel(eventData: IMouseEvent, x_delta: number, y_delta: number): void;
    sendFocus(focus: boolean): void;
    sendKeyClick(eventData: IKeyEvent, keyUp: boolean): void;
}

/**
 * Class representing an item within a scene.
 *
 * When you add an input source to a scene, a few things
 * happen. If the input source provides video, it allocates
 * rendering structures for it. If it provides audio, it
 * provides audio sampling structures for it. All actual
 * rendering information is held by the scene item. This
 * is so two scene items can be different even if they use
 * the same underlying source.
 *
 * Changing any of the properties will change how the
 * input source is rendered for that particular item.
 */
export interface ISceneItem {
    /** The underlying input source associated with this item */
    readonly source: IInput;

    /** The scene this item is in */
    readonly scene: IScene;

    /** The id assigned to this item when its created */
    readonly id: number;

    /** A flag determining whether the item is selected */
    selected: boolean;

    /** Position of the item */
    position: IVec2;

    /** Rotation of the in degrees */
    rotation: number;

    /** Scale of the item, with 1 being to scale */
    scale: IVec2;

    alignment: EAlignment;
    boundsAlignment: number;
    bounds: IVec2;

    /** How to apply bounds */
    boundsType: EBoundsType;

    /** How to apply scale */
    scaleFilter: EScaleType;

    /** Whether or not the item is visible */
    visible: boolean;

    /** Whether or not the item is visible on the streaming output */
    streamVisible: boolean;

    /** Whether or not the item is visible on the recording output */
    recordingVisible: boolean;

    /**
     * The canvas this item is routed to, or `null` when it renders on every canvas.
     * @throws {Error} If the item is no longer valid or if the OSN call fails.
     */
    get video(): IVideo | null;

    /**
     * Assign this item to a video canvas while preserving its caller-visible transform.
     * @throws {TypeError} If `value` is not an `IVideo` instance.
     * @throws {Error} If the item or supplied video canvas is no longer valid, or if the OSN call fails.
     */
    set video(value: IVideo);
    /**
     * Transform information on the item packed into
     * a single convenient object
     */
    transformInfo: ITransformInfo;

    /** Current crop applied to the item */
    crop: ICropInfo;

    /** Whether bounds crop the source when using a supported bounds type. */
    cropToBounds: boolean;

    /** Move the item towards the top-most item one spot */
    moveUp(): void;

    /** Move the item towards the bottom-most item one spot */
    moveDown(): void;

    /** Make the item the top-most item */
    moveTop(): void;

    /** Make the item the bottom-most item */
    moveBottom(): void;

    /**
     * Move the item to the specified position.
     * @param position Position relative to the bottom-most item
     */
    move(position: number): void;

    /** Remove the item from the scene it's attached to (destroys the item!) */
    remove(): void;

    /** Prevent updating of the item to prevent data races */
    deferUpdateBegin(): void;

    /** Allow updating of the item after calling {@link deferUpdateBegin} */
    deferUpdateEnd(): void;

    /** Set the item blending method */
    blendingMethod: EBlendingMethod;

    /** Set the item blending mode */
    blendingMode: EBlendingMode;
}

export interface ITransitionFactory extends IFactoryTypes {
    /**
     * Create a new instance of an ObsTransition
     * @param id - The type of transition source to create, possibly from {@link types}
     * @param name - Name of the created transition source
     * @param settings - Optional, settings to create transition source with
     * @param hotkeys - Optional, hotkey data associated with transition
     * @returns - Returns instance or null if failure
     */
    create(id: string, name: string, settings?: ISettings, hotkeys?: ISettings): ITransition;

    /**
     * Create a new instance of an ObsTransition that's private
     * Private in this context means any function that returns an
     * ObsTransition will not return this source
     * @param id - The type of transition source to create, possibly from {@link types}
     * @param name - Name of the created inptransitionut source
     * @param settings - Optional, settings to create transition source with
     * @returns - Returns instance or null if failure
     */
    createPrivate(id: string, name: string, settings?: ISettings): ITransition;

    fromName(name: string): ITransition;
}

/**
 * Class representing a transition
 */
export interface ITransition extends ISource {
    /**
     * Returns a list of available filter types for creation
     */

    /**
     * Obtain currently set input source.
     */
    getActiveSource(): ISource;

    /**
     * Clear the currently set input source
     */
    clear(): void;

    /**
     * Set a new input without transitioning.
     * @param input - Source to transition to
     */
    set(input: ISource): void;

    /**
     * Begins a transition into another scene/input source
     * @param ms - Length of time transition to new scene should take
     * @param input - Source to transition to
     */
    start(ms: number, input: ISource): void;

    /**
     * Re-trigger the transition's load step on the server.
     */
    load(): void;

    sendMouseClick(eventData: IMouseEvent, type: EMouseButtonType, mouseUp: boolean, clickCount: number): void;
    sendMouseMove(eventData: IMouseEvent, mouseLeave: boolean): void;
    sendMouseWheel(eventData: IMouseEvent, x_delta: number, y_delta: number): void;
    sendFocus(focus: boolean): void;
    sendKeyClick(eventData: IKeyEvent, keyUp: boolean): void;
}

export interface IConfigurable {
    /**
     * Update the settings of the source instance
     * correlating to the values held within the
     * object passed.
     */
    update(settings: ISettings): void;

    /**
     * Whether the source has properties or not
     */
    readonly configurable: boolean;

    /**
     * The properties of the source
     */
    readonly properties: IProperties;

    /**
     * Object holding current settings of the source
     */
    readonly settings: ISettings;
}

/**
 * Base class for Filter, Transition, Scene, and Input
 */
export interface ISource extends IConfigurable, IReleasable {
    /**
     * Send remove signal to other holders of the current reference.
     */
    remove(): void;

    /**
     * Send a save signal to sources themselves.
     * This should always be called before saving to disk
     * as it allows the source to know it needs to update
     * its settings.
     */
    save(): void;

    /**
     * Forward a serializable message to the underlying source plugin.
     * Note: only registered on input sources on the native side; calling on
     * a filter, scene, or transition will throw at runtime.
     */
    sendMessage(message: ISettings): void;

    /**
     * The validity of the source
     */
    readonly status: number;

    /**
     * Type of the source
     */
    readonly type: ESourceType;

    /**
     * The corresponding id of the source
     */
    readonly id: string;

    /**
     * Not to be confused with flags. This set
     * of flags provides the capabilities in the
     * output associated with the source. See
     * EOutputFlags for possible options. Is
     * represented as 32-bit binary flag.
     */
    readonly outputFlags: ESourceOutputFlags;

    /**
     * Name of the source when referencing it
     */
    name: string;

    /**
     * Unsigned bit-field concerning various flags
     */
    flags: ESourceFlags;

    /**
     * Muted flag, separate of the current volume
     */
    muted: boolean;

    /**
     * Whether or not the source is disabled.
     * Easy way to disable a filter.
     */
    enabled: boolean;

    /**
     * Function to get latest version of settings
	 * Expensive, shouldn't be used unless sure
     */
    readonly slowUncachedSettings: ISettings;

    /**
     * Executes a named function from obs internals
    */
     callHandler(fuction_name: string, fuction_input: string): Object;
}

export interface IFaderFactory {
    /**
     * Create an instance of a fader object
     * @param type - What algorithm to use for new fader.
     */
    create(type: EFaderType): IFader;
}

/**
 * Class representing a fader control corresponding to a source.
 */
export interface IFader {
    /**
     * Negative float representing volume using decibels.
     */
    db: number;

    /**
     * Percentage representing level of volume from 0% to 100%
     */
    deflection: number;

    /**
     * Multiplier representing volume levels
     */
    mul: number;

    /**
     * Destroy the fader object object
     */
    destroy(): void;

    /**
     * Attach to a source to monitor the volume of
     * @param source Input source to attach to
     */
    attach(source: IInput): void;

    /**
     * Detach from currently attached source.
     * Otherwise, is a no-op.
     */
    detach(): void;
}

export interface IVolmeterFactory {
    /**
     * Create an instance of a volmeter object
     * @param type - What algorithm to use for new fader.
     */
    create(type: EFaderType): IVolmeter;
}

/**
 * Object representing a volmeter control corresponding to a source.
 */
export interface IVolmeter {
    /**
     * Destroy the volmeter object object
     */
    destroy(): void;

    /**
     * Attaches to the volmeter object to a source
     * @param source Source to monitor the volume of
     */
    attach(source: IInput): void;

    /**
     * Detaches the currently attached source from the volmeter object
     */
    detach(): void;
}

/**
 * This is simply used to type check
 * objects passed back that hold internal
 * information when dealing with callbacks.
 */
export interface ICallbackData {
}

/**
 * This represents a obs_video_info structure from within libobs
 */
export interface IVideoInfo {
    fpsNum: number;
    fpsDen: number;
    baseWidth: number;
    baseHeight: number;
    outputWidth: number;
    outputHeight: number;
    outputFormat: EVideoFormat;
    colorspace: EColorSpace;
    range: ERangeType;
    scaleType: EScaleType;
    fpsType: EFPSType;
}

export interface IVideo {
    video: IVideoInfo;
    legacySettings: IVideoInfo;
    /**
     * Permanently destroys this video context.
     *
     * @throws {Error} If one or more scene items are assigned to this video
     * context. The context and this object remain valid.
     * @throws {Error} If libobs rejects removal before it begins, for example
     * because video is active. The context and this object remain valid.
     * @throws {Error} If removal completes but the remaining video contexts
     * cannot be initialized. The context is destroyed and this object is no
     * longer valid.
     * @throws {Error} If the OSN IPC call fails. In this case whether removal
     * completed may be unknown.
     */
    destroy(): void;
    /**
     * Number of total skipped frames
     */
     readonly skippedFrames: number;

     /**
      * Number of total encoded frames
      */
     readonly encodedFrames: number;

     /**
      * Server-side canvas id. Pass to APIs that reference video contexts by id.
      */
     readonly canvasId: number;
}

export interface IVideoFactory {
    create(): IVideo;
}

export interface IAudio {
    sampleRate: (44100 | 48000),
    speakers: ESpeakerLayout
}

export interface IDevice {
    name: string,
    id: string
}

export interface IAudioFactory {
    audioContext: IAudio;
    legacySettings: IAudio;
    monitoringDevice: IDevice;
    monitoringDeviceLegacy: IDevice;
    readonly monitoringDevices: IDevice[];
    disableAudioDucking: boolean; // Windows only
    disableAudioDuckingLegacy: boolean; // Windows only
}

export interface IModuleFactory {
    open(binPath: string, dataPath: string): IModule;
    modules(): String[];
}

export interface IModule {
    initialize(): void;
    readonly fileName: string;
    readonly name: string;
    readonly author: string;
    readonly description: string;
    readonly binaryPath: string;
    readonly dataPath: string;
}

export const NDI_RUNTIME_VERSION_MISMATCH = 'NDI_RUNTIME_VERSION_MISMATCH';
export const NDI_RUNTIME_NOT_FOUND = 'NDI_RUNTIME_NOT_FOUND';

export interface IObsModuleLoadFailure {
    module: string;
    code: string;
    message: string;
}

/**
 * Add multiple existing inputs to a scene with their initial transforms.
 * The returned items are initially unassigned and render on every canvas. Assign each item's `video`
 * before relying on canvas-specific routing or geometry.
 * @param scene - Scene that receives the items
 * @param sceneItems - Existing input names and their initial transforms
 * @returns The created, unassigned scene items
 * @throws {Error} If an input cannot be found or an item cannot be created
 */
export function addItems(scene: IScene, sceneItems: ISceneItemInfo[]): ISceneItem[] {
    const items: ISceneItem[] = [];
    if (Array.isArray(sceneItems)) {
        sceneItems.forEach(function(sceneItem) {
            const source = obs.Input.fromName(sceneItem.name);
            const item = scene.add(source, sceneItem);
            items.push(item);
        });
    }
    return items;
}

export interface FilterInfo {
    name: string,
    type: string,
    settings: ISettings,
    enabled: boolean
}

export interface SyncOffset {
    sec: number,
    nsec: number
}

export interface SourceInfo {
    filters: FilterInfo[],
    muted: boolean,
    name: string,
    settings: ISettings,
    type: string,
    volume: number,
    syncOffset: SyncOffset,
    deinterlaceMode: EDeinterlaceMode,
    deinterlaceFieldOrder: EDeinterlaceFieldOrder
}

export function createSources(sources: SourceInfo[]): IInput[] {
    const items: IInput[] = [];

    if (Array.isArray(sources)) {
        sources.forEach(function (source) {
            let newSource: IInput | null = null;

            try {
                newSource = obs.Input.create(source.type, source.name, source.settings);
            } catch (error) {
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
                        let ObsFilter: IFilter | null = null;

                        try {
                            ObsFilter = obs.Filter.create(filter.type, filter.name, filter.settings);
                        } catch (filterError) {
                            console.error(`[OSN] Failed to create filter "${filter.name}" for source "${source.name}":`, filterError instanceof Error ? filterError.message : filterError);
                        }

                        if (ObsFilter) {
                            ObsFilter.enabled = filter.enabled ?? true;
                            newSource.addFilter(ObsFilter);
                            ObsFilter.release();
                        }
                    });
                }
            } else {
                console.warn(`[OSN] Input creation failed for source: ${source.name}`);
            }
        });
    } else {
        console.error(`[OSN] Invalid sources array provided:`, sources);
    }

    return items;
}

export interface ISourceSize {
    name: string,
    width: number,
    height: number,
    outputFlags: number,
}
export function getSourcesSize(sourcesNames: string[]): ISourceSize[] {
    const sourcesSize: ISourceSize[] = [];
    if (Array.isArray(sourcesNames)) {
        sourcesNames.forEach(function (sourceName) {
            const ObsInput = obs.Input.fromName(sourceName);
            if(ObsInput) {
                sourcesSize.push({ name: sourceName, height: ObsInput.height, width: ObsInput.width, outputFlags: ObsInput.outputFlags });
            }
        });
    }
    return sourcesSize;
}
export interface IServiceFactory {
    types(): string[];
    create(id: string, name: string, settings?: ISettings): IService;
    destroy(stream: IService): void;
    legacySettings: IService;
}
/**
 * Class representing a service
 */
export interface IService {
    /** The service name */
    readonly name: string;

    /**
     * The properties of the service
     */
    readonly properties: IProperties;

    /**
     * Object holding current settings of the service
     */
    readonly settings: ISettings;

    /**
     * Update the settings of the service instance
     * correlating to the values held within the
     * object passed.
     */
    update(settings: ISettings): void;
}

export const enum ERecordingFormat {
    MP4 = 'mp4',
    FLV = 'flv',
    MOV = 'mov',
    MKV = 'mkv',
    MPEGTS = 'ts',
    HLS = 'm3u8'
}

export const enum ERecordingQuality {
    Stream,
    HighQuality,
    HigherQuality,
    Lossless
}

export const enum EVideoEncoderType {
    Audio,
    Video
}

export const enum EProcessPriority {
    High = 'High',
    AboveNormal = 'AboveNormal',
    Normal = 'Normal',
    BelowNormal = 'BelowNormal',
    Idle = 'Idle'
}

export interface IVideoEncoder extends IConfigurable, IReleasable {
    name: string,
    readonly type: EVideoEncoderType,
    readonly active: boolean,
    readonly id: string,
    readonly lastError: string
}

export interface IAudioEncoder extends IReleasable {
    name: string,
    bitrate: number
}

export interface IAudioEncoderFactory {
    create(id: string, name: string): IAudioEncoder
}

export interface IVideoEncoderFactory {
    types(): string[],
    types(filter: EVideoEncoderType): string[],
    create(id: string, name: string, settings?: ISettings): IVideoEncoder,
}

export interface IStreaming {
    // Video encoder value is ignored only in the Enhanced Broadcasting mode, otherwise it should be set
    videoEncoder?: IVideoEncoder,
    service: IService,
    enforceServiceBitrate: boolean,
    enableTwitchVOD: boolean,
    delay: IDelay,
    reconnect: IReconnect,
    network: INetwork,
    video: IVideo,
    signalHandler: (signal: EOutputSignal) => void,
    getAvailableEncoders(): IEncoderOption[],
    start(): void, // throws
    stop(force?: boolean): void,
    droppedFrames: number;
    totalFrames: number;
    kbitsPerSec: number;
    dataOutput: number;
}

export interface IEnhancedBroadcastingDisplayStats {
    kbitsPerSec: number;
    dataOutput: number;
}

export interface IEnhancedBroadcastingPerDisplayStats {
    horizontal: IEnhancedBroadcastingDisplayStats;
    vertical: IEnhancedBroadcastingDisplayStats;
}

export interface EOutputSignal {
    type: string,
    signal: string,
    code: number,
    error: string
}

export interface IEncoderOption {
    // UI display label for the encoder.
    title: string,
    // Mode-specific option value stored in OBS settings.
    name: string,
    // Concrete OBS encoder ID passed to VideoEncoderFactory.create().
    id: string,
    // Public Desktop encoder profile key, such as x264, qsv, nvenc, or amd.
    family: string,
    // OBS settings field that stores this encoder's preset value.
    preset: string,
    // OBS codec ID reported by the concrete encoder, such as h264, hevc, or av1.
    codec: string,
    // Whether this encoder option is allowed for streaming.
    streaming: boolean,
    // Whether this encoder option is allowed for recording.
    recording: boolean
}

export interface ISimpleStreaming extends IStreaming {
    audioEncoder: IAudioEncoder,
    useAdvanced: boolean,
    customEncSettings: string
}

export interface ISimpleStreamingFactory {
    create(): ISimpleStreaming;
    destroy(stream: ISimpleStreaming): void;
    legacySettings: ISimpleStreaming;
}

export interface IAdvancedStreaming extends IStreaming {
    audioTrack: number,
    twitchTrack: number,
    rescaling: boolean,
    rescaleFilter?: EScaleType,
    outputWidth?: number,
    outputHeight?: number
}

export interface IAdvancedStreamingFactory {
    create(): IAdvancedStreaming;
    destroy(stream: IAdvancedStreaming): void;
    legacySettings: IAdvancedStreaming;
}

export interface IEnhancedBroadcastingAdvancedStreaming extends IAdvancedStreaming {
    // If set, the Enhanced Broadcasting stream will be in the Dual Output mode.
    // This value should be initialized before the stream start.
    additionalVideo?: IVideo,
    displayStats: IEnhancedBroadcastingPerDisplayStats,
}

export interface IEnhancedBroadcastingAdvancedStreamingFactory {
    create(): IEnhancedBroadcastingAdvancedStreaming;
    destroy(stream: IEnhancedBroadcastingAdvancedStreaming): void;
    legacySettings: IEnhancedBroadcastingAdvancedStreaming;
}

export interface IEnhancedBroadcastingSimpleStreaming extends ISimpleStreaming {
    // If set, the Enhanced Broadcasting stream will be in the Dual Output mode.
    // This value should be initialized before the stream start.
    additionalVideo?: IVideo,
    displayStats: IEnhancedBroadcastingPerDisplayStats,
}

export interface IEnhancedBroadcastingSimpleStreamingFactory {
    create(): IEnhancedBroadcastingSimpleStreaming;
    destroy(stream: IEnhancedBroadcastingSimpleStreaming): void;
    legacySettings: IEnhancedBroadcastingSimpleStreaming;
}

export interface IFileOutput {
    path: string,
    format: ERecordingFormat,
    fileFormat: string,
    overwrite: boolean,
    noSpace: boolean,
    muxerSettings: string,
    video: IVideo,
    lastFile(): string
}

export interface IRecording extends IFileOutput {
    /**
     * Wrapped around fileFormat, space-separated, before the extension is added.
     * Set distinct values per canvas in dual output so the two recordings are
     * distinguishable on disk; otherwise both derive the same name and the second
     * one is renamed to "... (2)".
     */
    prefix: string,
    suffix: string,
    videoEncoder: IVideoEncoder,
    enableFileSplit: boolean,
    splitType: ERecSplitType,
    splitTime: number,
    splitSize: number,
    fileResetTimestamps: boolean,
    signalHandler: (signal: EOutputSignal) => void,
    getAvailableEncoders(): IEncoderOption[],
    start(): void,
    stop(force?: boolean): void,
    splitFile(): void
}

export interface ISimpleRecording extends IRecording {
    quality: ERecordingQuality,
    audioEncoder: IAudioEncoder,
    lowCPU: boolean,
    streaming: ISimpleStreaming
}

export interface IAdvancedRecording extends IRecording {
    mixer: number,
    rescaling: boolean,
    outputWidth?: number,
    outputHeight?: number,
    useStreamEncoders: boolean,
    streaming: IAdvancedStreaming
}

export interface ISimpleRecordingFactory {
    create(): ISimpleRecording;
    destroy(stream: ISimpleRecording): void;
    legacySettings: ISimpleRecording;
}

export interface IAdvancedRecordingFactory {
    create(): IAdvancedRecording;
    destroy(stream: IAdvancedRecording): void;
    legacySettings: IAdvancedRecording;
}

export interface IReplayBuffer extends IFileOutput {
    duration: number,
    prefix: string,
    suffix: string,
    usesStream: boolean,
    signalHandler: (signal: EOutputSignal) => void,
    start(): void,
    stop(force?: boolean): void,
    save(): void
}

export interface ISimpleReplayBuffer extends IReplayBuffer {
    streaming: ISimpleStreaming,
    recording: ISimpleRecording,
}

export interface IAdvancedReplayBuffer extends IReplayBuffer {
    mixer: number,
    streaming: IAdvancedStreaming,
    recording: IAdvancedRecording,
}

export interface ISimpleReplayBufferFactory {
    create(): ISimpleReplayBuffer;
    destroy(stream: ISimpleReplayBuffer): void;
    legacySettings: ISimpleReplayBuffer;
}

export interface IAdvancedReplayBufferFactory {
    create(): IAdvancedReplayBuffer;
    destroy(stream: IAdvancedReplayBuffer): void;
    legacySettings: IAdvancedReplayBuffer;
}

export interface IDelay {
    enabled: boolean,
    delaySec: number,
    preserveDelay: boolean
}

export interface IDelayFactory {
    create(): IDelay,
}

export interface IReconnect {
    enabled: boolean,
    retryDelay: number,
    maxRetries: number
}

export interface IReconnectFactory {
    create(): IReconnect
}

export interface INetwork {
    bindIP: string,
    readonly networkInterfaces: ISettings,
    enableDynamicBitrate: boolean,
    enableOptimizations: boolean,
    enableLowLatency: boolean
}

export interface INetworkFactory {
    create(): INetwork
}

export interface IAudioTrack {
    bitrate: number;
    name: string
}

export interface IAudioTrackFactory {
    create(bitrate: number, name: string): IAudioTrack;

    readonly audioTracks: IAudioTrack[];
    readonly audioBitrates: number[];
    getAtIndex(index: number): IAudioTrack;
    setAtIndex(audioTrack: IAudioTrack, index: number): void;

    importLegacySettings(): void;
    saveLegacySettings(): void;
}

// ---- Auto Optimizer API ----

type AutoConfigTopology =
    'direct-single' |
    'cloud-multistream' |
    'custom-rtmp' |
    'dual-output' |
    'enhanced-broadcasting' |
    'enhanced-broadcasting-dual-output' |
    'stream-shift' |
    'mixed';

type AutoConfigDisplay = 'horizontal' | 'vertical' | 'both';
type AutoConfigOutputKind = 'standard' | 'twitch-enhanced-broadcasting';

type AutoConfigPlatform =
    'twitch' |
    'youtube' |
    'facebook' |
    'kick' |
    'tiktok' |
    'custom' |
    'other';

type AutoConfigEstimateReason =
    'non_twitch' |
    'custom_rtmp' |
    'cloud_multistream' |
    'dual_output' |
    'enhanced_broadcasting' |
    'enhanced_broadcasting_dual_output' |
    'stream_shift' |
    'mixed_topology' |
    'probe_disabled' |
    'partial_provider_probes';

interface IAutoConfigDestination {
    platform: AutoConfigPlatform;
}

interface IAutoConfigCurrentSettings {
    /**
     * Zero-based server-side canvas identity from IVideo.canvasId; `0` is
     * valid. Active Enhanced Broadcasting and active two-leg Dual Output
     * require registered, distinct, nonnegative JavaScript-safe identities
     * that remain live for the native probe. Other request modes may omit the
     * identity.
     */
    canvasId?: number;
    width: number;
    height: number;
    fpsNum: number;
    fpsDen: number;
    bitrateKbps: number;
    encoderId: string;
    preset?: string;
}

interface IAutoConfigAdditionalVideoRequest {
    /** V1 paired Enhanced Broadcasting supports only the vertical canvas. */
    display: 'vertical';
    current: IAutoConfigCurrentSettings;
    limits?: IAutoConfigLimits;
}

interface IAutoConfigLimits {
    /**
     * Highest bitrate native may return for this Desktop-owned output. A
     * provider probe may intentionally test above this ceiling and preserve
     * the higher measured/safe capacity as stability or shared-uplink
     * evidence; that evidence never raises the returned bitrate above this
     * limit.
     */
    maxBitrateKbps?: number;
    /**
     * Complete attempt-scoped output ceiling eligible for isolated hardware
     * testing. It may exceed the current canvas because native does not mutate
     * persistent video settings while benchmarking. Supply maxWidth and
     * maxHeight together. Native caps this tuple to the V1 1080p tier and
     * promotes only exact 16:9 or 9:16 outputs. The caller remains responsible
     * for safely applying a recommended Base Canvas resize. Custom aspect
     * ratios retain their current resolution and frame rate.
     */
    maxWidth?: number;
    maxHeight?: number;
    /**
     * Complete frame-rate ceiling eligible for hardware testing. Supply
     * maxFpsNum to permit promotion above the current cadence; maxFpsDen
     * defaults to 1. Native caps the V1 benchmark at 60 or 59.94 FPS, and a
     * successful active provider probe is still required before returning a
     * higher frame rate as the recommendation.
     */
    maxFpsNum?: number;
    maxFpsDen?: number;
}

interface IAutoConfigLegRequest {
    legId: string;
    display: AutoConfigDisplay;
    /**
     * Physical output ownership. Composite Enhanced Broadcasting requests must
     * set this explicitly on every leg. Their single provider-owned leg is
     * Twitch-only and uses `display: 'both'`; each standard leg represents one
     * non-Twitch horizontal or vertical output.
     */
    outputKind: AutoConfigOutputKind;
    destinations: IAutoConfigDestination[];
    current: IAutoConfigCurrentSettings;
    limits?: IAutoConfigLimits;
    /**
     * Second canvas on the same Twitch Enhanced Broadcasting upload. Valid
     * only when display is `both`. Active probing is default-denied unless the
     * request has one Twitch destination, one Enhanced Broadcasting probe, and
     * two valid, distinct registered canvas identities.
     */
    additionalVideo?: IAutoConfigAdditionalVideoRequest;
    estimateReason?: AutoConfigEstimateReason;
}

interface IAutoConfigTwitchActiveProbe {
    probeId: string;
    kind: 'twitch-standard';
    legId: string;
    /** Official Twitch ingest URL; the service identity is derived from kind. */
    server: string;
    streamKey: string;
}

/** A safe full-ladder Twitch Enhanced Broadcasting capacity probe. */
interface IAutoConfigTwitchEnhancedBroadcastingProbe {
    probeId: string;
    kind: 'twitch-enhanced-broadcasting';
    legId: string;
    /** Native derives Twitch service identity and automatic ingest from kind. */
    /**
     * Twitch credential supplied only by Desktop's trusted worker. OSN
     * normalizes the bandwidth-test parameter, validates the final
     * Twitch-returned authentication before output starts, and clears its
     * request copy after probe setup.
     */
    streamKey: string;
}

interface IAutoConfigYoutubeActiveProbe {
    /**
     * Security contract: Desktop's trusted worker must create an exact-marked,
     * reusable-but-unbound liveStream and status-confirm that same resource.
     * Native validates the official RTMPS endpoint but cannot query YouTube
     * resource ownership or binding. Desktop must close the native session
     * before deleting the liveStream through the YouTube API.
     */
    probeId: string;
    kind: 'youtube-unbound';
    legId: string;
    /** Official YouTube RTMPS URL; the service identity is derived from kind. */
    server: string;
    streamKey: string;
}

type IAutoConfigActiveProbe =
    | IAutoConfigTwitchActiveProbe
    | IAutoConfigTwitchEnhancedBroadcastingProbe
    | IAutoConfigYoutubeActiveProbe;

/**
 * Complete immutable input for one Auto Optimizer session. OSN validates the
 * topology, canvas identities, provider probes, and limits when the session is
 * created. The caller owns all persistent settings and applies a returned
 * recommendation only after validating the corresponding result.
 */
export interface IAutoConfigRequest {
    schemaVersion: 1;
    topology: AutoConfigTopology;
    legs: IAutoConfigLegRequest[];
    /**
     * Attempt-scoped provider credentials. Native validates each probe
     * independently. A shared cloud leg may contain a subset of its probeable
     * destinations. A subset that succeeds during setup or execution produces
     * active evidence with low confidence rather than disabling every provider
     * measurement. Active Dual Output requires exactly one horizontal and one
     * vertical upload leg with distinct live canvas identities, one Twitch-
     * standard probe bound to a leg containing Twitch, and one YouTube-unbound
     * probe bound to the other leg containing YouTube. Either leg may contain
     * additional unprobed destinations. The probes execute sequentially, and
     * both must produce usable evidence before either leg is promoted.
     *
     * `enhanced-broadcasting-dual-output` is a separate exact topology. It
     * requires one Twitch Enhanced Broadcasting probe plus one or two standard
     * companion legs whose canvas identity and current video tuple exactly
     * match the corresponding primary or additional Twitch canvas. A companion
     * may optionally carry one YouTube probe; unsupported destinations remain
     * estimate-only, and custom RTMP is rejected. Standard probes finish before
     * the combined workload is tested.
     */
    activeProbes?: IAutoConfigActiveProbe[];
}

type AutoConfigEventType = 'phase' | 'progress' | 'result' | 'error' | 'cancelled' | 'complete';
type AutoConfigPhase = 'preflight' | 'hardware' | 'bandwidth' | 'recommendation' | 'cleanup';
type AutoConfigMeasurementMode = 'active' | 'estimated';

/** A paired vertical canvas tuple tested and applied with the primary canvas. */
interface IAutoConfigAdditionalVideoTuple {
    display: 'vertical';
    width: number;
    height: number;
    fpsNum: number;
    fpsDen: number;
}

/**
 * Ordered progress notification delivered to `NodeObs.AutoConfig.run()`.
 * `complete` and `cancelled` are terminal; the run handle then reads the result
 * and closes all native resources before settling its `result` promise.
 */
export interface IAutoConfigEvent {
    schemaVersion: 1;
    sessionId: string;
    sequence: number;
    type: AutoConfigEventType;
    phase: AutoConfigPhase;
    progress: number;
    /**
     * Machine-readable status or failure code. During hardware promotion,
     * hardware_testing_encoder_surfaces means OSN is validating the requested
     * resolution and public texture path at the available render cadence;
     * hardware_validating_target_cadence then validates the exact requested
     * resolution and frame rate through the hardware encoder's synthetic
     * raw-input counterpart. hardware_target_cadence_rejected means only that
     * quality candidate was rejected; OSN continues testing lower cadences and
     * keeps the public hardware encoder eligible. dual_output_testing_workload
     * marks the start of one concurrent two-leg encoder sample, while
     * dual_output_allocating_upload means both isolated provider probes and the
     * concurrent hardware sample passed and OSN is splitting their demonstrated
     * shared-uplink lower bound equally. During bandwidth testing,
     * twitch_probe_confirming_capacity means the initial Twitch window was
     * underfilled without transport pressure and OSN is running one extended
     * same-target window on the existing connection.
     * enhanced_broadcasting_testing_concurrent_outputs means Twitch's complete
     * returned ladder and every standard companion encoder are running in one
     * common five-second workload window. Companion sinks are video-only and
     * establish encoder/render capacity; they are not network bandwidth probes.
     */
    code?: string;
    legId?: string;
    measurementMode?: AutoConfigMeasurementMode;
    probeId?: string;
    provider?: 'twitch' | 'youtube';
    /** Applied video bitrate for the active probe substep; audio is additional. */
    targetBitrateKbps?: number;
    /** Concrete encoder currently being tested or selected. */
    encoderId?: string;
    /** Public family key matching getAvailableEncoders(). */
    encoderFamily?: string;
    /** User-facing encoder title from the native encoder catalog. */
    encoderTitle?: string;
    width?: number;
    height?: number;
    fpsNum?: number;
    fpsDen?: number;
    /** Vertical canvas tuple tested concurrently with the primary tuple. */
    additionalVideo?: IAutoConfigAdditionalVideoTuple;
    /** Final video bitrate selected with the quality tuple. */
    selectedBitrateKbps?: number;
    /** Effective safe video bandwidth used to select the quality tuple. */
    availableBitrateKbps?: number;
}

interface IAutoConfigProbeMeasurement {
    provider: 'twitch' | 'youtube';
    method: 'twitch-bandwidth-test' | 'twitch-enhanced-broadcasting-test' | 'youtube-unbound-ramp';
    /**
     * True when the probe produced sufficient evidence for its recommendation.
     * Standard Twitch and YouTube probes validate a usable safeKbps value. An
     * Enhanced Broadcasting probe instead validates that the complete returned
     * video ladder sustained the exact tested canvas tuple; it does not imply a
     * measured or safe upload-bandwidth value.
     */
    success: boolean;
    /**
     * Aggregate output payload rate observed during the probe. This is
     * sender-side evidence, not receiver-confirmed network capacity: the
     * source or encoder can underfill its target without network loss or
     * congestion.
     */
    measuredKbps?: number;
    /**
     * Validated video capacity derived from usable probe evidence and
     * applicable platform caps. It can exceed the final recommendation when a
     * higher stability rung was tested; only explicit degradation or transport
     * evidence can lower it. This is not necessarily the raw observed aggregate
     * payload rate.
     */
    safeKbps?: number;
    /**
     * Fixed percentage removed from usable probe evidence. Current probe
     * policies validate targets directly and report zero; the field remains in
     * the measurement contract so callers can explain future policies.
     */
    headroomPercent?: number;
    /**
     * True when the active ladder reached its effective probe ceiling. The
     * ceiling can come from probe, platform, or request limits and does not
     * imply that the physical upload path was saturated.
     */
    ceilingReached: boolean;
    /** Exact canvas width validated by a successful Enhanced Broadcasting probe; omitted for other or failed probes. */
    testedWidth?: number;
    /** Exact canvas height validated by a successful Enhanced Broadcasting probe; omitted for other or failed probes. */
    testedHeight?: number;
    /** Exact frame-rate numerator validated by a successful Enhanced Broadcasting probe; omitted for other or failed probes. */
    testedFpsNum?: number;
    /** Exact frame-rate denominator validated by a successful Enhanced Broadcasting probe; omitted for other or failed probes. */
    testedFpsDen?: number;
    /** Exact vertical tuple exercised concurrently with the primary Enhanced Broadcasting tuple. */
    testedAdditionalVideo?: IAutoConfigAdditionalVideoTuple;
    /** Returned video tracks exercised concurrently by a successful Enhanced Broadcasting probe; omitted otherwise. */
    videoTrackCount?: number;
    /**
     * Sum of the configured video and live-audio bitrates in the validated
     * Enhanced Broadcasting ladder. This is requested workload, not observed
     * throughput or path capacity, and is omitted for other or failed probes.
     */
    configuredAggregateBitrateKbps?: number;
}

interface IAutoConfigMeasurement {
    mode: AutoConfigMeasurementMode;
    confidence: 'high' | 'medium' | 'low';
    reason?: string;
    /**
     * Evidence only for providers whose probes were attempted. For a
     * partial_provider_probes result, compare these providers only with the
     * leg's probe-capable Twitch and YouTube destinations to identify which
     * supported providers were estimated. Other co-destinations are outside
     * the V1 bandwidth-probing contract and must not be labelled as estimated.
     */
    probes?: IAutoConfigProbeMeasurement[];
}

interface IAutoConfigRecommendation {
    width: number;
    height: number;
    fpsNum: number;
    fpsDen: number;
    bitrateKbps: number;
    encoderId: string;
    encoderFamily: string;
    encoderTitle: string;
    codec: string;
    preset?: string;
    /** Paired vertical canvas recommendation validated in the same Enhanced Broadcasting output. */
    additionalVideo?: IAutoConfigAdditionalVideoTuple;
}

interface IAutoConfigResultDestination {
    platform: string;
}

interface IAutoConfigLegResult {
    legId: string;
    display: AutoConfigDisplay;
    /** Echoes request ownership so callers can reject mismatched or reclassified legs. */
    outputKind: AutoConfigOutputKind;
    destinations: IAutoConfigResultDestination[];
    measurement: IAutoConfigMeasurement;
    recommendation: IAutoConfigRecommendation;
    limits?: IAutoConfigLimits;
}

interface IAutoConfigAggregateUpload {
    /** Two isolated provider probes establish a lower bound for one shared uplink. */
    method: 'dual-output-isolated-lower-bound';
    /** Larger isolated safe result: the aggregate video budget demonstrated across the two sequential probes. */
    safeVideoKbps: number;
    /** Sum of the equal, 100-Kbps-rounded video allocations returned for both legs. */
    allocatedVideoKbps: number;
    /** True only when both leg encoder workloads passed concurrently in the same benchmark window. */
    concurrentHardwareValidated: true;
}

interface IAutoConfigCombinedWorkloadCompanionLeg {
    legId: string;
    display: 'horizontal' | 'vertical';
    width: number;
    height: number;
    fpsNum: number;
    fpsDen: number;
    /** Desktop-owned outputs never exceed 8000 Kbps; provider-owned ladders ignore this field. */
    bitrateKbps: number;
    encoderId: string;
    preset?: string;
}

/**
 * Exact video-encoder workloads sustained concurrently with Twitch's returned
 * ladder. This proves local render/encoder capacity only; provider bandwidth
 * provenance remains in each leg's `measurement.probes`.
 */
interface IAutoConfigCombinedWorkload {
    method: 'enhanced-broadcasting-dual-output-concurrent';
    enhancedBroadcastingLegId: string;
    validated: true;
    companionLegs: IAutoConfigCombinedWorkloadCompanionLeg[];
}

type AutoConfigFatalErrorCode =
    | 'cancelled'
    | 'hardware_no_usable_encoder'
    | 'hardware_benchmark_overloaded'
    | 'hardware_benchmark_timeout'
    | 'hardware_benchmark_unavailable'
    | 'autoconfig_worker_failed'
    | 'autoconfig_worker_launch_failed';

interface IAutoConfigError {
    code: AutoConfigFatalErrorCode;
}

/**
 * Final, caller-owned Auto Optimizer result. The `result` promise does not
 * settle until native outputs have stopped and the session is closed. No
 * further progress callback invocations occur after it settles, so provider
 * resources can be deleted safely after awaiting it.
 */
export interface IAutoConfigResult {
    schemaVersion: 1;
    sessionId: string;
    status: 'complete' | 'partial' | 'cancelled' | 'failed';
    error?: IAutoConfigError;
    /** Present only for a fully successful active Twitch + YouTube Dual Output recommendation. */
    aggregateUpload?: IAutoConfigAggregateUpload;
    /**
     * Present only after the full Enhanced Broadcasting plus every companion
     * workload passes one common sample window. Standard leg recommendations
     * reuse these exact tuples, encoder IDs, presets, and bitrates.
     */
    combinedWorkload?: IAutoConfigCombinedWorkload;
    legs: IAutoConfigLegResult[];
}

/** One running Auto Optimizer operation. */
interface IAutoConfigRun {
    /**
     * Settles with the native result only after native outputs have stopped and
     * the session is closed. No further progress callback invocations occur
     * after settlement. It rejects on malformed native data, a reported IPC
     * failure, or cleanup failure. A caller that must bound a lost native
     * process should enforce its own deadline and call `cancel()`.
     */
    readonly result: Promise<IAutoConfigResult>;

    /**
     * Supplies the caller's authoritative ingest observation for the active
     * YouTube probe.
     * @throws {Error} If this run is already closed or the native probe cannot
     * accept the confirmation
     */
    confirmProbeIngest(probeId: string, received: boolean): void;

    /**
     * Cancels native work and resolves after the same close barrier as
     * `result`. Calling it again is idempotent after a successful close and
     * retries the close barrier if the preceding cleanup attempt failed.
     * @throws {Error} If the native close barrier cannot be completed
     */
    cancel(): Promise<void>;
}

/** High-level, resource-safe Auto Optimizer API. */
interface IAutoConfig {
    /**
     * Creates and immediately starts one optimizer run. The progress callback
     * is registered before native work starts. Provider credentials are copied
     * into native request JSON and are not retained by the returned handle.
     *
     * @param request - Immutable attempt-scoped request
     * @param onProgress - Receives validated, ordered progress events
     * @returns A cancellable run whose result owns the native close barrier
     * @throws {TypeError} If the request or callback has the wrong type
     * @throws {Error} If request validation or session creation fails
     *
     * A native start failure is reported through `result`, rather than thrown,
     * because the returned handle retains the close barrier needed to release
     * any provider-owned resources safely. `cancel()` remains retryable if that
     * initial close attempt fails.
     */
    run(request: IAutoConfigRequest, onProgress: (event: IAutoConfigEvent) => void): IAutoConfigRun;
}

/**
 * Most of the addon's non-optimizer surface remains dynamically typed. This
 * interface gives the Auto Optimizer methods useful declarations without
 * changing unrelated callers.
 */
interface INodeObs {
    [key: string]: any;

    /** Resource-safe Auto Optimizer entry point. */
    readonly AutoConfig: IAutoConfig;

    /**
     * Initializes the global OBS runtime.
     * @param options - Required runtime initialization options
     * @returns The OBS video initialization result code
     * @throws {TypeError} If exactly one options object is not provided, or if a required option is not a string
     * @throws {Error} If the IPC call fails or OSN returns an error response without an initialization result
     */
    OBS_API_initAPI(options: IOBSAPIInitializationOptions): EVideoCodes;
}

export const enum VCamOutputType {
	Invalid,
	SceneOutput,
	SourceOutput,
	ProgramView,
	PreviewOutput,
};

// Initialization and other stuff which needs local data.
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
export const NodeObs: INodeObs = obs;
