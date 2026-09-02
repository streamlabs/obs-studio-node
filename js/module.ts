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
    /** Required for standard streaming. Twitch supplies the Enhanced Broadcasting encoding ladder. */
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
    /** Optional vertical canvas for Enhanced Broadcasting Dual Output. Assign it before calling `start()`. */
    additionalVideo?: IVideo,
    displayStats: IEnhancedBroadcastingPerDisplayStats,
}

export interface IEnhancedBroadcastingAdvancedStreamingFactory {
    create(): IEnhancedBroadcastingAdvancedStreaming;
    destroy(stream: IEnhancedBroadcastingAdvancedStreaming): void;
    legacySettings: IEnhancedBroadcastingAdvancedStreaming;
}

export interface IEnhancedBroadcastingSimpleStreaming extends ISimpleStreaming {
    /** Optional vertical canvas for Enhanced Broadcasting Dual Output. Assign it before calling `start()`. */
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

type AutoConfigStreamSetup =
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

interface IAutoConfigCurrentSettings {
    /**
     * Canvas ID from `IVideo.canvasId`; `0` is valid. Active Enhanced
     * Broadcasting and two-output Dual Output require distinct IDs for
     * registered canvases that remain alive for the entire run. Other stream
     * setups may omit this field.
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
    /**
     * The parent `current` describes the horizontal canvas; this secondary
     * video request describes the vertical canvas.
     */
    display: 'vertical';
    current: IAutoConfigCurrentSettings;
    limits?: IAutoConfigLimits;
}

interface IAutoConfigLimits {
    /**
     * Maximum video bitrate OSN may recommend for an output whose encoding
     * settings are managed by Desktop. A provider probe may test above this
     * value to verify stability or shared upload capacity, but the
     * recommendation never exceeds it.
     */
    maxBitrateKbps?: number;
    /**
     * Maximum resolution OSN may test for this run without changing persistent
     * video settings. Supply `maxWidth` and `maxHeight` together. OSN tests only
     * the supported 1920x1080, 1280x720, and 960x540 tiers, or their portrait
     * equivalents, up to this limit. It promotes only 16:9 or 9:16 output; a
     * custom aspect ratio keeps its current resolution and frame rate. The
     * caller remains responsible for applying a recommended Base Canvas resize
     * safely.
     */
    maxWidth?: number;
    maxHeight?: number;
    /**
     * Maximum frame rate OSN may test. Supply `maxFpsNum` to permit promotion
     * above the current frame rate; `maxFpsDen` defaults to `1`. OSN tests at
     * most 60 FPS, or 60000/1001 when `maxFpsDen` is `1001`. It returns a
     * higher frame rate only after a successful active provider probe.
     */
    maxFpsNum?: number;
    maxFpsDen?: number;
}

interface IAutoConfigOutputRequest {
    outputId: string;
    display: AutoConfigDisplay;
    /**
     * Identifies who supplies the encoding settings. Use `standard` when
     * Desktop selects the encoder and bitrate. Use
     * `twitch-enhanced-broadcasting` when Twitch supplies the ladder. That
     * Twitch-only output uses `display: 'horizontal'` for one canvas or
     * `display: 'both'` for paired horizontal and vertical canvases. Each
     * non-Twitch output remains `standard`.
     */
    outputKind: AutoConfigOutputKind;
    destinations: AutoConfigPlatform[];
    current: IAutoConfigCurrentSettings;
    limits?: IAutoConfigLimits;
    /**
     * Optional vertical canvas carried on the same Twitch Enhanced
     * Broadcasting upload as `current`. Valid only with `display: 'both'`. OSN
     * permits active probing only for one Twitch destination, one Enhanced
     * Broadcasting probe, and two distinct registered canvas IDs.
     */
    additionalVideo?: IAutoConfigAdditionalVideoRequest;
    estimateReason?: AutoConfigEstimateReason;
    /** Probes for this run, executed in array order. */
    probes?: IAutoConfigProbeRequest[];
}

interface IAutoConfigTwitchProbeRequest {
    id: string;
    kind: 'twitch-standard';
    /** Official Twitch ingest URL. OSN selects the Twitch service from `kind`. */
    server: string;
    streamKey: string;
}

/** Twitch Enhanced Broadcasting probe that tests the complete returned encoding ladder. */
interface IAutoConfigTwitchEnhancedBroadcastingProbeRequest {
    id: string;
    kind: 'twitch-enhanced-broadcasting';
    /**
     * Twitch stream key supplied by Desktop's trusted worker. OSN selects the
     * Twitch service from `kind`, normalizes the bandwidth-test parameter,
     * verifies the authentication returned by Twitch before starting output,
     * and clears its temporary copy after configuring the probe.
     */
    streamKey: string;
}

/**
 * Desktop's trusted worker must create a reusable YouTube `liveStream` with
 * the required Auto Optimizer marker and leave it unbound to a
 * `liveBroadcast`. Desktop must confirm ingest for that same resource. OSN
 * validates the official RTMPS endpoint but cannot verify YouTube resource
 * ownership or binding. Await the run's cleanup before deleting the
 * `liveStream`.
 */
interface IAutoConfigYoutubeProbeRequest {
    id: string;
    kind: 'youtube-unbound';
    /** Official YouTube RTMPS URL. OSN selects the YouTube service from `kind`. */
    server: string;
    streamKey: string;
}

type IAutoConfigProbeRequest =
    | IAutoConfigTwitchProbeRequest
    | IAutoConfigTwitchEnhancedBroadcastingProbeRequest
    | IAutoConfigYoutubeProbeRequest;

/**
 * Input for one Auto Optimizer run. OSN validates the stream setup, canvas IDs,
 * provider probes, and limits before starting. It never changes persistent
 * settings; the caller validates and applies any recommendation.
 */
export interface IAutoConfigRequest {
    streamSetup: AutoConfigStreamSetup;
    /**
     * Outputs and provider probes for this run. OSN validates each probe
     * independently. A shared cloud output may probe only some of its measurable
     * destinations; successful partial coverage produces active evidence with
     * low confidence instead of disabling all provider measurement.
     *
     * Active Dual Output requires one horizontal and one vertical output with
     * distinct live canvas IDs. One output containing Twitch must have a
     * `twitch-standard` probe, and the other output containing YouTube must have
     * a `youtube-unbound` probe. Either output may include additional unprobed
     * destinations. The probes run sequentially, and both must produce usable
     * evidence before OSN promotes either output.
     *
     * `enhanced-broadcasting-dual-output` requires one Twitch Enhanced
     * Broadcasting probe and one or two standard outputs. Each standard output
     * must match the canvas ID, resolution, and frame rate of the corresponding
     * Twitch canvas. A standard output may include one YouTube probe;
     * unsupported destinations remain estimate-only, and custom RTMP is
     * rejected. Standard probes finish before OSN tests the combined workload.
     */
    outputs: IAutoConfigOutputRequest[];
}

type AutoConfigEventType = 'phase' | 'progress' | 'result' | 'error' | 'cancelled' | 'complete';
type AutoConfigPhase = 'preflight' | 'hardware' | 'bandwidth' | 'recommendation' | 'cleanup';
type AutoConfigMeasurementMode = 'active' | 'estimated';

/** Vertical canvas settings tested together with the primary canvas. */
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
 * and releases the run's temporary OSN resources before settling its `result`
 * promise.
 */
export interface IAutoConfigEvent {
    type: AutoConfigEventType;
    phase: AutoConfigPhase;
    progress: number;
    /**
     * Machine-readable status or failure code. Important progress codes include:
     *
     * - `hardware_testing_encoder_surfaces`: OSN tests the requested resolution
     *   through the selected texture encoder at the available render frame rate.
     * - `hardware_validating_target_cadence`: OSN tests the exact requested
     *   resolution and frame rate through the encoder's synthetic raw-input
     *   counterpart.
     * - `hardware_target_cadence_rejected`: only this resolution and frame-rate
     *   candidate was rejected; OSN continues with lower candidates and keeps
     *   the selected hardware encoder eligible.
     * - `dual_output_testing_workload`: OSN starts one concurrent two-output
     *   encoder sample.
     * - `dual_output_allocating_upload`: both provider probes and the concurrent
     *   encoder sample passed, so OSN divides the demonstrated shared upload
     *   capacity equally.
     * - `twitch_probe_confirming_capacity`: an initial Twitch sample was
     *   underfilled without transport pressure, so OSN repeats it for longer on
     *   the existing connection.
     * - `enhanced_broadcasting_testing_concurrent_outputs`: OSN runs Twitch's
     *   complete returned ladder and every standard companion encoder in one
     *   five-second window. The video-only companion outputs test local encoding
     *   and rendering capacity, not network bandwidth.
     */
    code?: string;
    outputId?: string;
    measurementMode?: AutoConfigMeasurementMode;
    probe?: IAutoConfigEventProbe;
    /** Applied video bitrate for the active probe substep; audio is additional. */
    targetBitrateKbps?: number;
    /** Concrete encoder currently being tested or selected. */
    encoderId?: string;
    /** Public family key matching getAvailableEncoders(). */
    encoderFamily?: string;
    /** User-facing encoder title from OSN's encoder catalog. */
    encoderTitle?: string;
    width?: number;
    height?: number;
    fpsNum?: number;
    fpsDen?: number;
    /** Vertical canvas settings tested together with the primary canvas settings. */
    additionalVideo?: IAutoConfigAdditionalVideoTuple;
    /** Video bitrate selected for the recommended resolution and frame rate. */
    selectedBitrateKbps?: number;
    /** Conservative video-bandwidth estimate used to choose the recommended resolution and frame rate. */
    availableBitrateKbps?: number;
}

interface IAutoConfigMeasurementEvidence {
    platform: 'twitch' | 'youtube';
    method: 'twitch-bandwidth-test' | 'twitch-enhanced-broadcasting-test' | 'youtube-unbound-ramp';
    success: boolean;
}

interface IAutoConfigMeasurement {
    mode: AutoConfigMeasurementMode;
    confidence: 'high' | 'medium' | 'low';
    reason?: string;
    /** Provider measurements that contributed to the result. Detailed throughput and workload data remains internal to OSN. */
    evidence?: IAutoConfigMeasurementEvidence[];
}

interface IAutoConfigVideoRecommendation {
    display: 'horizontal' | 'vertical';
    width: number;
    height: number;
    fpsNum: number;
    fpsDen: number;
}

interface IAutoConfigEncodingRecommendation {
    bitrateKbps: number;
    encoderId: string;
    encoderFamily: string;
    encoderTitle: string;
    codec: string;
    preset?: string;
}

interface IAutoConfigOutputResult {
    outputId: string;
    /** One recommended canvas setting, or separate horizontal and vertical settings for a paired output. */
    videos: IAutoConfigVideoRecommendation[];
    /** Omitted for Twitch Enhanced Broadcasting because Twitch supplies its encoding ladder. */
    encoding?: IAutoConfigEncodingRecommendation;
    measurement: IAutoConfigMeasurement;
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
 * Final Auto Optimizer result. The `result` promise settles only after OSN
 * stops every temporary output and closes the session. No progress callback
 * runs after settlement, so the caller may then delete temporary provider
 * resources.
 */
export interface IAutoConfigResult {
    status: 'complete' | 'partial' | 'cancelled' | 'failed';
    error?: IAutoConfigError;
    outputs: IAutoConfigOutputResult[];
}

/** One running Auto Optimizer operation. */
interface IAutoConfigRun {
    /**
     * Settles after OSN stops its temporary outputs and closes the session. No
     * progress callback runs after settlement. It rejects on malformed server
     * data, an IPC failure, or cleanup failure. To bound a server process that
     * stops responding, enforce a caller-side deadline and call `cancel()`.
     */
    readonly result: Promise<IAutoConfigResult>;

    /**
     * Reports whether YouTube received data for the active probe.
     * @throws {Error} If this run is already closed or the probe cannot
     * accept the confirmation
     */
    confirmProbeIngest(probeId: string, received: boolean): void;

    /**
     * Requests cancellation and resolves after OSN closes the session. Repeated
     * calls are safe after successful cleanup and retry cleanup if the preceding
     * attempt failed.
     * @throws {Error} If OSN cannot complete session cleanup
     */
    cancel(): Promise<void>;
}

type AutoConfigProbeKind =
    | 'twitch-standard'
    | 'twitch-enhanced-broadcasting'
    | 'youtube-unbound';

interface IAutoConfigEventProbe {
    id: string;
    kind: AutoConfigProbeKind;
}

/** Auto Optimizer API that manages each run through cleanup. */
interface IAutoConfig {
    /**
     * Creates and immediately starts one optimizer run. The progress callback
     * is registered before work starts. Credentials are used only to configure
     * probes and are not stored on the returned handle.
     *
     * @param request - Input used only for this run
     * @param onProgress - Receives validated, ordered progress events
     * @returns A cancellable run whose result settles after session cleanup
     * @throws {TypeError} If the request or callback has the wrong type
     * @throws {Error} If request validation or session creation fails
     *
     * A server start failure is reported through `result`, rather than thrown,
     * so the returned handle can still complete cleanup before the caller
     * releases temporary provider resources. `cancel()` remains retryable if
     * that initial cleanup attempt fails.
     */
    run(request: IAutoConfigRequest, onProgress: (event: IAutoConfigEvent) => void): IAutoConfigRun;
}

/**
 * Typed Auto Optimizer surface on the add-on's otherwise dynamic export.
 */
interface INodeObs {
    [key: string]: any;

    /** Starts and manages Auto Optimizer runs. */
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
