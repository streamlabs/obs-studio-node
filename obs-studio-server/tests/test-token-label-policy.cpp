#include "token-label-policy.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

class ScopedDuplicatedToken {
public:
	ScopedDuplicatedToken()
	{
		HANDLE process_token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &process_token))
			return;

		DuplicateTokenEx(process_token, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation, TokenImpersonation, &token);
		CloseHandle(process_token);
	}

	~ScopedDuplicatedToken()
	{
		if (token)
			CloseHandle(token);
	}

	HANDLE token = nullptr;
};

osn::cef::TokenLabelPolicy WithMask(osn::cef::TokenLabelPolicy policy, DWORD mask)
{
	policy.mask = mask;
	return policy;
}

} // namespace

TEST_CASE("Token label policy is restored after CEF hardening changes it", "[cef-sandbox]")
{
	ScopedDuplicatedToken duplicated;
	REQUIRE(duplicated.token != nullptr);

	const auto original = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(original.has_value());
	REQUIRE_FALSE(original->sid.empty());

	constexpr DWORD hardened_mask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP | SYSTEM_MANDATORY_LABEL_NO_READ_UP | SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP;
	REQUIRE(osn::cef::RestoreTokenLabelPolicy(duplicated.token, WithMask(*original, hardened_mask)));

	const auto hardened = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(hardened.has_value());
	CHECK(hardened->mask == hardened_mask);

	REQUIRE(osn::cef::RestoreTokenLabelPolicy(duplicated.token, *original));

	const auto restored = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(restored.has_value());
	CHECK(restored->mask == original->mask);
	CHECK(restored->sid == original->sid);
}

TEST_CASE("Token label policy restore rejects an empty policy", "[cef-sandbox]")
{
	ScopedDuplicatedToken duplicated;
	REQUIRE(duplicated.token != nullptr);

	CHECK_FALSE(osn::cef::RestoreTokenLabelPolicy(duplicated.token, osn::cef::TokenLabelPolicy{}));
}
