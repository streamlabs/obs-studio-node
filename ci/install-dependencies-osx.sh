#!/bin/bash
set -e

# Remove preinstalled deprecated/disabled formulae and casks
brew uninstall --ignore-dependencies openssl@1.1 2>/dev/null || true
brew uninstall --cask session-manager-plugin 2>/dev/null || true

# Remove unnecessary local taps (these cause warnings on GitHub runners)
brew untap homebrew/core 2>/dev/null || true
brew untap homebrew/cask 2>/dev/null || true

# Clean up broken symlinks and outdated packages
brew cleanup 2>/dev/null || true

# Update Homebrew
brew update

# Only install cmake if not already present
if ! command -v cmake &> /dev/null; then
    brew install cmake
fi

# Verify cmake
cmake --version

corepack enable
corepack prepare yarn@4.9.1 --activate

# Install module dependencies
yarn install
yarn add electron@${ElectronVersion}