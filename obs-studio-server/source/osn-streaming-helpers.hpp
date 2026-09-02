/******************************************************************************
    Copyright (C) 2016-2022 by Streamlabs (General Workings Inc)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

******************************************************************************/

#pragma once

#include <obs.h>
#include <cstring>

namespace osn {
namespace streaming_helpers {

// Resolves the registered OBS output type for a service. The returned ID is
// non-owned and remains valid while its output type is registered.
inline const char *getStreamOutputType(const obs_service_t *service)
{
	if (!service)
		return nullptr;

	const char *protocol = obs_service_get_protocol(service);
	if (!protocol) {
		blog(LOG_WARNING, "The service '%s' has no protocol set", obs_service_get_id(service));
		return nullptr;
	}

	if (!obs_is_output_protocol_registered(protocol)) {
		blog(LOG_WARNING, "The protocol '%s' is not registered", protocol);
		return nullptr;
	}

	// Prefer the service's explicit output type when it is usable.
	const char *output = obs_service_get_preferred_output_type(service);
	if (output && (obs_get_output_flags(output) & OBS_OUTPUT_SERVICE) != 0)
		return output;
	if (output)
		blog(LOG_WARNING, "The output '%s' is not registered, fallback to another one", output);

	const auto canUseOutput = [](const char *protocol, const char *outputType, const char *protocol1, const char *protocol2 = nullptr) {
		return (std::strcmp(protocol, protocol1) == 0 || (protocol2 && std::strcmp(protocol, protocol2) == 0)) &&
		       (obs_get_output_flags(outputType) & OBS_OUTPUT_SERVICE) != 0;
	};
	if (canUseOutput(protocol, "rtmp_output", "RTMP", "RTMPS"))
		return "rtmp_output";
	if (canUseOutput(protocol, "ffmpeg_hls_muxer", "HLS"))
		return "ffmpeg_hls_muxer";
	if (canUseOutput(protocol, "ffmpeg_mpegts_muxer", "SRT", "RIST"))
		return "ffmpeg_mpegts_muxer";

	// Third-party protocols may register their own service output.
	const auto returnFirstOutputId = [](void *data, const char *id) {
		*static_cast<const char **>(data) = id;
		return false;
	};
	obs_enum_output_types_with_protocol(protocol, &output, returnFirstOutputId);
	if (output)
		return output;

	blog(LOG_WARNING, "No output compatible with the service '%s' is registered", obs_service_get_id(service));
	return nullptr;
}

} // namespace streaming_helpers
} // namespace osn
