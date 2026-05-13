"use strict";

const { spawnSync } = require("child_process");

const buildDirectory =
  process.env.SLBUILDDIRECTORY ||
  process.env.SLBuildDirectory ||
  process.env.BUILD_DIRECTORY ||
  "build";
const buildConfig = process.env.BUILD_CONFIG || process.env.BuildConfig || "RelWithDebInfo";
const target = "obs_studio_client_unit_tests";
// Match the TEST_PREFIX passed to catch_discover_tests so ctest only runs this unit-test suite.
const testPattern = `^${target}::`;

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

run("cmake", ["--build", buildDirectory, "--config", buildConfig, "--target", target]);
run("ctest", ["--test-dir", buildDirectory, "-C", buildConfig, "--output-on-failure", "-R", testPattern]);
