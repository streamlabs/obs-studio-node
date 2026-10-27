#include "token-label-policy.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

class ScopedDuplicatedToken {
public:
	explicit ScopedDuplicatedToken(DWORD access = MAXIMUM_ALLOWED)
	{
		HANDLE process_token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &process_token))
			return;

		DuplicateTokenEx(process_token, access, nullptr, SecurityImpersonation, TokenImpersonation, &token);
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

TEST_CASE("Clearing NO_READ_UP leaves the other label bits and the SID intact", "[cef-sandbox]")
{
	ScopedDuplicatedToken duplicated;
	REQUIRE(duplicated.token != nullptr);

	const auto original = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(original.has_value());
	REQUIRE_FALSE(original->sid.empty());

	constexpr DWORD hardened_mask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP | SYSTEM_MANDATORY_LABEL_NO_READ_UP | SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP;
	REQUIRE(osn::cef::SetTokenLabelPolicy(duplicated.token, WithMask(*original, hardened_mask)));

	const auto hardened = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(hardened.has_value());
	CHECK(hardened->mask == hardened_mask);

	REQUIRE(osn::cef::SetTokenLabelPolicy(duplicated.token, WithMask(*hardened, hardened->mask & ~static_cast<DWORD>(SYSTEM_MANDATORY_LABEL_NO_READ_UP))));

	const auto cleared = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(cleared.has_value());
	CHECK(cleared->mask == (SYSTEM_MANDATORY_LABEL_NO_WRITE_UP | SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP));
	CHECK(cleared->sid == original->sid);

	REQUIRE(osn::cef::SetTokenLabelPolicy(duplicated.token, *original));

	const auto restored = osn::cef::CaptureTokenLabelPolicy(duplicated.token);
	REQUIRE(restored.has_value());
	CHECK(restored->mask == original->mask);
	CHECK(restored->sid == original->sid);
}

TEST_CASE("Token label policy set rejects an empty policy", "[cef-sandbox]")
{
	ScopedDuplicatedToken duplicated;
	REQUIRE(duplicated.token != nullptr);

	CHECK_FALSE(osn::cef::SetTokenLabelPolicy(duplicated.token, osn::cef::TokenLabelPolicy{}));
}

TEST_CASE("Token label policy set reports the failure through last-error", "[cef-sandbox]")
{
	ScopedDuplicatedToken writable;
	REQUIRE(writable.token != nullptr);
	const auto original = osn::cef::CaptureTokenLabelPolicy(writable.token);
	REQUIRE(original.has_value());

	ScopedDuplicatedToken read_only(TOKEN_QUERY | READ_CONTROL);
	REQUIRE(read_only.token != nullptr);

	SetLastError(ERROR_SUCCESS);
	CHECK_FALSE(osn::cef::SetTokenLabelPolicy(read_only.token, *original));
	CHECK(GetLastError() == ERROR_ACCESS_DENIED);
}