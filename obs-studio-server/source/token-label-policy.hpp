#pragma once

#ifdef _WIN32

#include <optional>
#include <vector>

#include <Windows.h>

namespace osn::cef {

struct TokenLabelPolicy {
	std::vector<BYTE> sid;
	DWORD mask = 0;
};

std::optional<TokenLabelPolicy> CaptureTokenLabelPolicy(HANDLE token);
bool SetTokenLabelPolicy(HANDLE token, const TokenLabelPolicy &policy);

} // namespace osn::cef

#endif
