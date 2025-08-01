#!/bin/bash

echo "BambuStudio Mac Build Script"
echo "============================"
echo ""

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to install dependencies via Homebrew
install_dependencies() {
    echo "Installing build dependencies..."
    
    # Update Homebrew
    echo "Updating Homebrew..."
    brew update
    
    # Install required packages
    echo "Installing required packages..."
    brew install cmake ninja git gettext pcre
    
    # Check if Xcode command line tools are installed
    if ! xcode-select -p &> /dev/null; then
        echo "Xcode Command Line Tools not found. Installing..."
        xcode-select --install
        echo "Please follow the installation prompts and then re-run this script."
        exit 1
    fi
}

# Check and install dependencies
echo "Checking dependencies..."
if ! command_exists cmake; then
    echo "CMake not found."
    install_dependencies
elif ! command_exists ninja; then
    echo "Ninja not found."
    install_dependencies
else
    echo "All dependencies are installed."
fi

# Set up environment
export ARCH=$(uname -m)
echo "Building for architecture: $ARCH"

# Change to BambuStudio directory
cd "$(dirname "$0")"

# First, build dependencies
echo ""
echo "Step 1: Building dependencies..."
echo "This may take 30-60 minutes on first build..."
chmod +x BuildMac.sh
./BuildMac.sh -d

# Check if deps build was successful
if [ $? -ne 0 ]; then
    echo "Error: Dependencies build failed!"
    exit 1
fi

# Then build BambuStudio
echo ""
echo "Step 2: Building BambuStudio..."
echo "This may take 15-30 minutes..."
./BuildMac.sh -s

# Check if build was successful
if [ $? -ne 0 ]; then
    echo "Error: BambuStudio build failed!"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo "The BambuStudio app is located at:"
echo "build/$ARCH/BambuStudio/BambuStudio.app"
echo ""
echo "You can run it with:"
echo "open build/$ARCH/BambuStudio/BambuStudio.app"