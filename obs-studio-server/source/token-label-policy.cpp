#include "token-label-policy.hpp"

#ifdef _WIN32

#include <aclapi.h>

namespace osn::cef {

std::optional<TokenLabelPolicy> CaptureTokenLabelPolicy(HANDLE token)
{
	PACL sacl = nullptr;
	PSECURITY_DESCRIPTOR descriptor = nullptr;
	if (GetSecurityInfo(token, SE_KERNEL_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, &sacl, &descriptor) != ERROR_SUCCESS)
		return std::nullopt;

	std::optional<TokenLabelPolicy> policy;
	for (DWORD index = 0; sacl && index < sacl->AceCount && !policy; ++index) {
		void *ace = nullptr;
		if (!GetAce(sacl, index, &ace))
			break;

		if (static_cast<ACE_HEADER *>(ace)->AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE)
			continue;

		const auto label = static_cast<SYSTEM_MANDATORY_LABEL_ACE *>(ace);
		const PSID sid = reinterpret_cast<PSID>(&label->SidStart);
		const BYTE *sid_bytes = static_cast<const BYTE *>(sid);
		policy = TokenLabelPolicy{std::vector<BYTE>(sid_bytes, sid_bytes + GetLengthSid(sid)), label->Mask};
	}

	LocalFree(descriptor);
	return policy;
}

bool SetTokenLabelPolicy(HANDLE token, const TokenLabelPolicy &policy)
{
	if (policy.sid.empty())
		return false;

	const PSID sid = const_cast<BYTE *>(policy.sid.data());
	std::vector<BYTE> buffer(sizeof(ACL) + sizeof(SYSTEM_MANDATORY_LABEL_ACE) + GetLengthSid(sid));
	const PACL sacl = reinterpret_cast<PACL>(buffer.data());
	if (!InitializeAcl(sacl, static_cast<DWORD>(buffer.size()), ACL_REVISION))
		return false;
	if (!AddMandatoryAce(sacl, ACL_REVISION, 0, policy.mask, sid))
		return false;

	// SetSecurityInfo returns the error code without setting last-error.
	const DWORD status = SetSecurityInfo(token, SE_KERNEL_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, sacl);
	if (status != ERROR_SUCCESS) {
		SetLastError(status);
		return false;
	}
	return true;
}

} // namespace osn::cef

#endif
