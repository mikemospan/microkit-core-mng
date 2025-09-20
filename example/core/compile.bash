# !/bin/bash

# Set the environment variables for the build
MICROKIT_BOARD="qemu_virt_aarch64"
MICROKIT_SDK="../../release/microkit-sdk-2.0.1-dev"
MICROKIT_CONFIG="debug"
NUM_CPUS=4

# Check if the build directory exists; if not, create it
BUILD_DIR="./build"
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

# Run the make command with the specified parameters
make BUILD_DIR=$BUILD_DIR MICROKIT_BOARD=$MICROKIT_BOARD MICROKIT_SDK=$MICROKIT_SDK MICROKIT_CONFIG=$MICROKIT_CONFIG NUM_CPUS=$NUM_CPUS -B qemu