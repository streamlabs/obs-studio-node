#pragma once

namespace osn::tests {
// This helper object uses RAII pattern to initialize & destroy OBS API
class ObsSetup {
public:
	ObsSetup();
	~ObsSetup();
};
} // namespace osn::tests
