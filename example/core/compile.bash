#!/bin/bash
set -e

# --- Configuration ---
EXAMPLE_DIR="example/core"
BUILD_DIR="build"
MICROKIT_BOARD="maaxboard_4_cores"
MICROKIT_SDK="release/microkit-sdk-2.0.1-dev"
MICROKIT_CONFIG="debug"
NUM_CPUS=4
SEL4_DIR="../seL4"

# --- Parse arguments ---
REBUILD_SDK=false
while getopts "B" opt; do
    case "$opt" in
        B) REBUILD_SDK=true ;;
        *) echo "Usage: $0 [-B]"; exit 1 ;;
    esac
done

# --- Ensure correct working directory ---
if [ ! -d "$EXAMPLE_DIR" ]; then
    echo "Error: $EXAMPLE_DIR not found. Please run this script from the project root directory."
    exit 1
fi

# --- Optionally rebuild Microkit SDK ---
if [ "$REBUILD_SDK" = true ]; then
    echo "Rebuilding Microkit SDK..."
    python3 build_sdk.py --sel4="$SEL4_DIR" --boards "$MICROKIT_BOARD" --skip-doc
    echo "SDK rebuild complete."
fi

# --- Prepare build directory ---
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

# --- Run make ---
echo "Building project..."
make -C "$EXAMPLE_DIR" \
    BUILD_DIR="$BUILD_DIR" \
    MICROKIT_BOARD="$MICROKIT_BOARD" \
    MICROKIT_SDK="../../$MICROKIT_SDK" \
    MICROKIT_CONFIG="$MICROKIT_CONFIG" \
    NUM_CPUS="$NUM_CPUS" \
    -B
