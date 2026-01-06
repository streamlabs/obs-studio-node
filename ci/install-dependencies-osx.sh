#!/bin/bash
set -e

# Clean up Homebrew to remove deprecated/broken entries and broken symlinks
brew cleanup 2>/dev/null || true
brew untap homebrew/core 2>/dev/null || true
brew untap homebrew/cask 2>/dev/null || true

# Update Homebrew
brew update
brew doctor || true

# Install minimal system dependencies
# Note: Node.js is managed by actions/setup-node, not Homebrew
# OpenSSL is built statically in libcurl (CURL_USE_OPENSSL is OFF in CMakeLists.txt)
# Python comes with macOS, no need to install

brew install cmake

# Verify cmake installation
cmake --version

# Install module dependencies
yarn install
yarn add electron@${ElectronVersion}