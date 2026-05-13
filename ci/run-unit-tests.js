"use strict";

const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const buildDirectory =
  process.env.SLBUILDDIRECTORY ||
  process.env.SLBuildDirectory ||
  process.env.BUILD_DIRECTORY ||
  "build";
const buildConfig = process.env.BUILD_CONFIG || process.env.BuildConfig || "RelWithDebInfo";
const target = "obs_studio_client_unit_tests";
// Match the TEST_PREFIX passed to catch_discover_tests so ctest only runs this unit-test suite.
const testPattern = `^${target}::`;

function hasDiscoveredTests() {
  const testsDirectory = path.join(buildDirectory, "obs-studio-client");

  try {
    return fs
      .readdirSync(testsDirectory)
      .some(fileName => fileName.startsWith(`${target}-`) && fileName.endsWith("_tests.cmake"));
  } catch {
    return false;
  }
}

function run(command, args) {
  const result = spawnSync(command, args, { stdio: "inherit" });

  if (result.error) {
    console.error(result.error.message);
    process.exit(1);
  }

  if (result.status !== 0) {
    process.exit(result.status);
  }
}

const skipBuild =
  process.env.OSN_SKIP_UNIT_TEST_BUILD === "1" ||
  // Test jobs run from uploaded build artifacts. Building again can force CMake
  // to reconfigure FetchContent checkouts whose hidden .git directories were not uploaded.
  (process.env.GITHUB_ACTIONS === "true" && hasDiscoveredTests());

if (skipBuild) {
  console.log("Skipping unit-test build; discovered CTest tests are already present.");
} else {
  run("cmake", ["--build", buildDirectory, "--config", buildConfig, "--target", target]);
}

run("ctest", ["--test-dir", buildDirectory, "-C", buildConfig, "--output-on-failure", "-R", testPattern]);
