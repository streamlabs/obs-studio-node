#pragma once

#include <obs.hpp>

#include <string>
#include <nlohmann/json_fwd.hpp>

namespace util {

/**
 * Returns the input serialized to JSON, but any non-empty "authorization"
 * properties have their values replaced by "CENSORED".
 */
std::string censoredJson(obs_data_t *data, bool pretty = false);
std::string censoredJson(nlohmann::json data, bool pretty = false);

}