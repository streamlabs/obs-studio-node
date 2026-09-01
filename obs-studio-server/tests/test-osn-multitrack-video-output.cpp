#include <catch2/catch_test_macros.hpp>

#include "osn-multitrack-video-output.hpp"

TEST_CASE("Twitch bandwidth-test authentication is normalized fail closed", "[enhanced-broadcasting][bandwidth-test]")
{
	SECTION("a bare key receives one enabled parameter")
	{
		const auto key = osn::NormalizeTwitchBandwidthTestKey("live_key");
		REQUIRE(key == "live_key?bandwidthtest=true");
		REQUIRE(osn::HasExactlyOneTwitchBandwidthTestParameter(key));
	}

	SECTION("existing parameters survive while bandwidth-test variants are replaced")
	{
		const auto key = osn::NormalizeTwitchBandwidthTestKey("live_key?foo=bar&BANDWIDTHTEST=false&%62andwidthtest=TRUE");
		REQUIRE(key == "live_key?foo=bar&bandwidthtest=true");
		REQUIRE(osn::HasExactlyOneTwitchBandwidthTestParameter(key));
	}

	SECTION("raw parameters retain their ordering and malformed escapes")
	{
		const auto key = osn::NormalizeTwitchBandwidthTestKey("live_key?first=%6G&bandwidth%74est=false&tail=%&second=a+b#fragment");
		REQUIRE(key == "live_key?first=%6G&tail=%&second=a+b&bandwidthtest=true");
		REQUIRE(osn::HasExactlyOneTwitchBandwidthTestParameter(key));
	}

	SECTION("hexadecimal escapes accept uppercase and lowercase digits")
	{
		REQUIRE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?ba%6Edwidthtest=tr%75e"));
		REQUIRE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?ba%6edwidthtest=TRUE"));
	}

	SECTION("malformed and incomplete escapes remain literal")
	{
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest=tr%7Ge"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest=tru%6"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtes%G0=true"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtes%74%=true"));
	}

	SECTION("plus signs use form-query space semantics")
	{
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest=t+rue"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest+=true"));
	}

	SECTION("only a query before the fragment participates")
	{
		REQUIRE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest=true#fragment?bandwidthtest=false"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key#fragment?bandwidthtest=true"));
		REQUIRE(osn::NormalizeTwitchBandwidthTestKey("live_key#fragment?bandwidthtest=false") == "live_key?bandwidthtest=true");
	}

	SECTION("missing, disabled, valueless, and duplicate parameters are rejected")
	{
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest=false"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest"));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter("live_key?bandwidthtest=true&bandwidthtest=true"));
	}

	SECTION("an empty or whitespace-only credential remains visibly invalid")
	{
		const auto empty = osn::NormalizeTwitchBandwidthTestKey("");
		const auto whitespace = osn::NormalizeTwitchBandwidthTestKey("  ");
		REQUIRE(empty == "?bandwidthtest=true");
		REQUIRE(whitespace == "?bandwidthtest=true");
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter(empty));
		REQUIRE_FALSE(osn::HasExactlyOneTwitchBandwidthTestParameter(whitespace));
	}
}

TEST_CASE("Twitch bandwidth-test parameters merge into the effective authentication", "[enhanced-broadcasting][bandwidth-test]")
{
	SECTION("the submitted key is not duplicated when no replacement authentication is returned")
	{
		const auto requested = osn::NormalizeTwitchBandwidthTestKey("live_key?foo=bar");
		const auto merged = osn::MergeTwitchBandwidthTestKey(requested, requested);
		CHECK(merged == requested);
		CHECK(osn::HasExactlyOneTwitchBandwidthTestParameter(merged));
	}

	SECTION("caller safety parameters override values on replacement authentication")
	{
		const auto requested = osn::NormalizeTwitchBandwidthTestKey("submitted?foo=requested");
		const auto merged = osn::MergeTwitchBandwidthTestKey("returned?bandwidthtest=false&foo=returned&auth=kept", requested);
		CHECK(merged == "returned?auth=kept&foo=requested&bandwidthtest=true");
		CHECK(osn::HasExactlyOneTwitchBandwidthTestParameter(merged));
	}

	SECTION("selected URL queries and client config survive without duplicates")
	{
		const auto requested = osn::NormalizeTwitchBandwidthTestKey("submitted?foo=requested");
		const auto merged = osn::MergeTwitchBandwidthTestKey("returned?auth=kept&clientConfigId=old", requested,
								     "rtmp://example/app?urlToken=kept&BANDWIDTHTEST=false", "new-config");
		CHECK(merged == "returned?urlToken=kept&auth=kept&foo=requested&bandwidthtest=true&clientConfigId=new-config");
		CHECK(osn::HasExactlyOneTwitchBandwidthTestParameter(merged));
	}
}
