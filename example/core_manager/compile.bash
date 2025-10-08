#!/bin/bash
set -e

# --- Configuration ---
EXAMPLE_DIR="example/core_manager"
BUILD_DIR="build"
MICROKIT_BOARDS=("qemu_virt_aarch64" "odroidc4_4_cores" "maaxboard_4_cores")
MICROKIT_SDK="release/microkit-sdk-2.0.1-dev"
MICROKIT_CONFIG="debug"
NUM_CPUS=4
SEL4_DIR="../seL4"

# --- Default variables ---
REBUILD_SDK=false
BOARD=""
CORE_SYSTEM=""

# --- Parse arguments ---
while getopts "Rb:" opt; do
    case "$opt" in
        R)
            REBUILD_SDK=true
            ;;
        b)
            BOARD="$OPTARG"
            CORE_SYSTEM="system/${BOARD}.system"
            ;;
        *)
            echo "Usage: $0 -b <board> [-R]"
            echo ""
            echo "Required:"
            echo "  -b <board>   Specify which board system file to use"
            echo ""
            echo "Optional:"
            echo "  -R           Rebuild Microkit SDK before building"
            echo ""
            echo "Available boards:"
            for b in "${MICROKIT_BOARDS[@]}"; do
                echo "  $b"
            done
            exit 1
            ;;
    esac
done

# --- Ensure board was provided ---
if [ -z "$BOARD" ]; then
    echo "Error: You must specify a board with -b <board>"
    echo ""
    echo "Available boards:"
    for b in "${MICROKIT_BOARDS[@]}"; do
        echo "  $b"
    done
    echo ""
    echo "Usage: $0 -b <board> [-R]"
    exit 1
fi

# --- Validate board name ---
if [[ ! " ${MICROKIT_BOARDS[*]} " =~ " ${BOARD} " ]]; then
    echo "Error: Invalid board '${BOARD}'"
    echo ""
    echo "Valid options are:"
    for b in "${MICROKIT_BOARDS[@]}"; do
        echo "  $b"
    done
    echo ""
    echo "Usage: $0 -b <board> [-R]"
    exit 1
fi

# --- Determine CPU based on board ---
case "$BOARD" in
    qemu_virt_aarch64|maaxboard_4_cores)
        CPU="cortex-a53"
        ;;
    odroidc4_4_cores)
        CPU="cortex-a55"
        ;;
    *)
        echo "Unknown board '$BOARD', defaulting CPU to cortex-a53"
        CPU="cortex-a53"
        ;;
esac

# --- Ensure correct working directory ---
if [ ! -d "$EXAMPLE_DIR" ]; then
    echo "Error: $EXAMPLE_DIR not found. Please run this script from the project root directory."
    exit 1
fi

# --- Ensure system file exists ---
if [ ! -f "$EXAMPLE_DIR/$CORE_SYSTEM" ]; then
    echo "Error: System file '$EXAMPLE_DIR/$CORE_SYSTEM' not found."
    exit 1
fi

# --- Optionally rebuild Microkit SDK ---
if [ "$REBUILD_SDK" = true ]; then
    echo "Rebuilding Microkit SDK..."
    python3 build_sdk.py --sel4="$SEL4_DIR" --boards "$(IFS=,; echo "${MICROKIT_BOARDS[*]}")" --skip-doc
    echo "SDK rebuild complete."
fi

# --- Prepare build directory ---
mkdir -p "$BUILD_DIR"

# --- Set up the make targets ---
MAKE_TARGETS="-B"
if [ "$BOARD" = "qemu_virt_aarch64" ]; then
    MAKE_TARGETS="$MAKE_TARGETS qemu"
fi

# --- Run make ---
echo "Building project for board: $BOARD"
make -C "$EXAMPLE_DIR" \
    BUILD_DIR="$BUILD_DIR" \
    MICROKIT_BOARD="$BOARD" \
    MICROKIT_SDK="../../$MICROKIT_SDK" \
    MICROKIT_CONFIG="$MICROKIT_CONFIG" \
    CORE_SYSTEM="$CORE_SYSTEM" \
    CPU="$CPU" \
    NUM_CPUS="$NUM_CPUS" \
    $MAKE_TARGETS
