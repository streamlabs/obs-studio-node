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

const testSuites = [
  { target: "obs_studio_client_unit_tests", sourceDir: "obs-studio-client" },
  { target: "obs_studio_server_unit_tests", sourceDir: "obs-studio-server" },
];

function hasDiscoveredTests(target, sourceDir) {
  const testsDirectory = path.join(buildDirectory, sourceDir);

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

for (const { target, sourceDir } of testSuites) {
  const testPattern = `^${target}::`;

  const skipBuild =
    process.env.OSN_SKIP_UNIT_TEST_BUILD === "1" ||
    (process.env.GITHUB_ACTIONS === "true" && hasDiscoveredTests(target, sourceDir));

  if (skipBuild) {
    console.log(`Skipping build for ${target}; discovered CTest tests are already present.`);
  } else {
    run("cmake", ["--build", buildDirectory, "--config", buildConfig, "--target", target]);
  }

  run("ctest", ["--test-dir", buildDirectory, "-C", buildConfig, "--output-on-failure", "-R", testPattern]);
}
