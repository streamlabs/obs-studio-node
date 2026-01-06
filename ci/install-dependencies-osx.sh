#!/bin/bash
set -e

# Clean up Homebrew to remove deprecated/broken entries
brew cleanup 2>/dev/null || true
brew untap homebrew/core 2>/dev/null || true
brew untap homebrew/cask 2>/dev/null || true

# Update Homebrew
brew update
brew doctor || true

# Install system dependencies (avoiding deprecated versions)
# Using explicit versions to prevent deprecated formula warnings
brew install cmake
brew install python@3.11
brew install openssl@3

# Export PATH for consistency
export PATH="/usr/local/opt/python@3.11/bin:$PATH"

# Verify installations
python3 --version
cmake --version

# Install module dependencies
yarn install
yarn add electron@${ElectronVersion}