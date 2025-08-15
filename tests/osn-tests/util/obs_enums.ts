// Enums
export const enum EOBSOutputType {
    Streaming = 'streaming',
    Recording = 'recording',
    ReplayBuffer = 'replay-buffer',
    VirtualCam = 'virtual-camera',
}
  
export const enum EOBSOutputSignal {
    Starting = 'starting',
    Start = 'start',
    Activate = 'activate',
    Stopping = 'stopping',
    Stop = 'stop',
    Deactivate = 'deactivate',
    Reconnect = 'reconnect',
    ReconnectSuccess = 'reconnect_success',
    Writing = 'writing',
    Wrote = 'wrote',
    WriteError = 'writing_error',
}

export const enum EOBSInputTypes {
    AudioLine = 'audio_line',
    ImageSource = 'image_source',
    ColorSource = 'color_source',
    Slideshow = 'slideshow',
    BrowserSource = 'browser_source',
    FFMPEGSource = 'ffmpeg_source',
    TextGDI = 'text_gdiplus',
    TextFT2 = 'text_ft2_source',
    VLCSource = 'vlc_source',
    MonitorCapture = 'monitor_capture',
    WindowCapture = 'window_capture',
    GameCapture = 'game_capture',
    DShowInput = 'dshow_input',
    WASAPIInput = 'wasapi_input_capture',
    WASAPIOutput = 'wasapi_output_capture',
    AVCaptureInput = 'av_capture_input',
    CoreAudioInput = 'coreaudio_input_capture',
    CoreAudioOutput = 'coreaudio_output_capture'
}

export const enum EOBSFilterTypes {
    FaceMask = 'face_mask_filter',
    Mask = 'mask_filter',
    Crop = 'crop_filter',
    Gain = 'gain_filter',
    Color = 'color_filter',
    Scale = 'scale_filter',
    Scroll = 'scroll_filter',
    GPUDelay = 'gpu_delay',
    ColorKey = 'color_key_filter',
    Clut = 'clut_filter',
    Sharpness = 'sharpness_filter',
    ChromaKey = 'chroma_key_filter',
    AsyncDelay = 'async_delay_filter',
    NoiseSuppress = 'noise_suppress_filter',
    InvertPolarity = 'invert_polarity_filter',
    NoiseGate = 'noise_gate_filter',
    Compressor = 'compressor_filter',
    Limiter = 'limiter_filter',
    Expander = 'expander_filter',
    LumaKey = 'luma_key_filter',
    NDI = 'ndi_filter',
    NDIAudio ='ndi_audiofilter',
    PremultipliedAlpha = 'premultiplied_alpha_filter',
    VST = 'vst_filter'
}

export const enum EOBSTransitionTypes {
    Cut = 'cut_transition',
    Fade = 'fade_transition',
    Swipe = 'swipe_transition',
    Slide = 'slide_transition',
    Stinger = 'obs_stinger_transition',
    FadeToColor = 'fade_to_color_transition',
    Wipe = 'wipe_transition'
}

export const enum EOBSSettingsCategories {
    General = 'General',
    Stream = 'Stream',
    StreamSecond = 'StreamSecond',
    Output = 'Output',
    Audio = 'Audio',
    Video = 'Video',
    Hotkeys = 'Hotkeys',
    Advanced = 'Advanced'
}