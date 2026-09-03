/******************************************************************************
    Copyright (C) 2026 by Streamlabs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.h>

namespace autoOptimizer::videoMix {

constexpr obs_video_rendering_mode activeRenderingMode(bool multipleRendering)
{
	return multipleRendering ? OBS_STREAMING_VIDEO_RENDERING : OBS_MAIN_VIDEO_RENDERING;
}

} // namespace autoOptimizer::videoMix
