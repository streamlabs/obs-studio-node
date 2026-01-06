#!/bin/bash
set -e

# Remove preinstalled deprecated/disabled formulae and casks that trigger warnings
brew uninstall --ignore-dependencies openssl@1.1 node@20 2>/dev/null || true
brew uninstall --cask session-manager-plugin 2>/dev/null || true
brew cleanup 2>/dev/null || true

# Update Homebrew (keep taps intact)
brew update
brew doctor || true

# Install minimal system dependency (Node is provided by actions/setup-node)
brew install cmake

# Verify
cmake --version

# Install module dependencies
yarn install
yarn add electron@${ElectronVersion}