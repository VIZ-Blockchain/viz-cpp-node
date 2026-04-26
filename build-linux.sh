#!/usr/bin/env bash

# ============================================================================
#  VIZ Linux Build Script
#
#  Prerequisites:
#    - Install build dependencies first (once, as root):
#        sudo ./install-deps-linux.sh
#    - Ubuntu 20.04+ (24.04 Noble recommended for system Boost 1.74)
#    - CMake 3.16+
#    - GCC 8+ or Clang 3.3+
#    - Boost 1.71+
#    - Git
#
#  Usage:
#    ./build-linux.sh [options]
#
#  Options:
#    -t, --type TYPE         Build type: Release or Debug (default: Release)
#    -l, --lowmem            Build low memory node (LOW_MEMORY_NODE=ON)
#    -n, --testnet           Build for testnet (BUILD_TESTNET=ON)
#    -m, --mongo             Enable MongoDB plugin (ENABLE_MONGO_PLUGIN=ON)
#    -s, --static            Build shared libraries OFF (static linking)
#    --no-lock-check         Disable chainbase lock checking
#    --clean                 Remove and recreate the build directory before build
#    --install               Run make install after build
#    -j, --jobs N            Number of parallel jobs (default: nproc)
#    --boost-root PATH       Custom Boost installation path
#    --openssl-root PATH     Custom OpenSSL installation path
#    -h, --help              Show this help message
# ============================================================================

set -euo pipefail

# --- Defaults ---
BUILD_TYPE="Release"
LOW_MEMORY="OFF"
BUILD_TESTNET="OFF"
ENABLE_MONGO="OFF"
SHARED_LIBS="OFF"
CHAINBASE_LOCK="ON"
CLEAN_BUILD="false"
DO_INSTALL="false"
JOBS=""
BOOST_ROOT_ARG=""
OPENSSL_ROOT_ARG=""

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# --- Refuse to run as root ---
# Build must run as a regular user; use sudo ./install-deps-linux.sh for deps.
if [[ $EUID -eq 0 ]]; then
    error "Do not run this script as root. Install dependencies first with: sudo ./install-deps-linux.sh"
fi

# --- Parse arguments ---
show_help() {
    sed -n '2,/^# =====/s/^#  //p' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"; shift 2 ;;
        -l|--lowmem)
            LOW_MEMORY="ON"; shift ;;
        -n|--testnet)
            BUILD_TESTNET="ON"; shift ;;
        -m|--mongo)
            ENABLE_MONGO="ON"; shift ;;
        -s|--static)
            SHARED_LIBS="OFF"; shift ;;
        --no-lock-check)
            CHAINBASE_LOCK="OFF"; shift ;;
        --clean)
            CLEAN_BUILD="true"; shift ;;
        --install)
            DO_INSTALL="true"; shift ;;
        -j|--jobs)
            JOBS="$2"; shift 2 ;;
        --boost-root)
            BOOST_ROOT_ARG="-DBOOST_ROOT=$2"; shift 2 ;;
        --openssl-root)
            OPENSSL_ROOT_ARG="-DOPENSSL_ROOT_DIR=$2"; shift 2 ;;
        -h|--help)
            show_help ;;
        *)
            error "Unknown option: $1. Use -h for help." ;;
    esac
done

# --- Detect number of jobs ---
if [[ -z "$JOBS" ]]; then
    JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 2)
fi

# --- Determine source directory ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR"

# --- Check we're in the right directory ---
if [[ ! -f "$SOURCE_DIR/CMakeLists.txt" ]]; then
    error "CMakeLists.txt not found in $SOURCE_DIR. Run this script from the viz-cpp-node root."
fi

# --- Initialize submodules ---
if [[ -d "$SOURCE_DIR/.git" ]]; then
    info "Initializing git submodules..."
    git -C "$SOURCE_DIR" submodule update --init --recursive -f
fi

# --- Create (or clean) build directory ---
BUILD_DIR="$SOURCE_DIR/build"

if [[ "$CLEAN_BUILD" == "true" && -d "$BUILD_DIR" ]]; then
    warn "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# --- Display configuration ---
echo ""
echo "============================================"
echo " VIZ Linux Build"
echo "============================================"
echo " Build Type:       $BUILD_TYPE"
echo " Low Memory Node:  $LOW_MEMORY"
echo " Build Testnet:    $BUILD_TESTNET"
echo " Enable MongoDB:   $ENABLE_MONGO"
echo " Shared Libs:      $SHARED_LIBS"
echo " Chainbase Locks:  $CHAINBASE_LOCK"
echo " Clean Build:      $CLEAN_BUILD"
echo " Parallel Jobs:    $JOBS"
echo " Source Dir:       $SOURCE_DIR"
echo " Build Dir:        $BUILD_DIR"
if [[ -n "$BOOST_ROOT_ARG" ]]; then
echo " Boost:            $BOOST_ROOT_ARG"
fi
if [[ -n "$OPENSSL_ROOT_ARG" ]]; then
echo " OpenSSL:          $OPENSSL_ROOT_ARG"
fi
echo "============================================"
echo ""

# --- Configure ---
info "[1/3] Configuring with CMake..."
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBUILD_SHARED_LIBRARIES="$SHARED_LIBS" \
    -DLOW_MEMORY_NODE="$LOW_MEMORY" \
    -DBUILD_TESTNET="$BUILD_TESTNET" \
    -DENABLE_MONGO_PLUGIN="$ENABLE_MONGO" \
    -DCHAINBASE_CHECK_LOCKING="$CHAINBASE_LOCK" \
    -DBoost_NO_BOOST_CMAKE=ON \
    $BOOST_ROOT_ARG \
    $OPENSSL_ROOT_ARG

info "CMake configuration complete."

# --- Build ---
# Use make directly instead of cmake --build to avoid jobserver conflicts
# with legacy ExternalProject targets (secp256k1 inside fc).
info "[2/3] Building vizd with $JOBS parallel jobs..."
make -C "$BUILD_DIR" -j"$JOBS" vizd

info "Build complete."

# --- Install ---
if [[ "$DO_INSTALL" == "true" ]]; then
    info "[3/3] Installing..."
    sudo make -C "$BUILD_DIR" install
    info "Installation complete."
else
    info "[3/3] Skipping install (use --install to install)."
fi

echo ""
echo "============================================"
echo -e " ${GREEN}Build completed successfully!${NC}"
echo " Binary: $BUILD_DIR/programs/vizd/vizd"
echo "============================================"
