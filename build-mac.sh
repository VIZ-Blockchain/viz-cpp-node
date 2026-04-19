#!/usr/bin/env bash
# ============================================================================
#  VIZ macOS Build Script
#
#  Prerequisites:
#    - macOS 12 (Monterey) or later
#    - Xcode Command Line Tools (xcode-select --install)
#    - Homebrew (https://brew.sh)
#    - Boost 1.71+ (via Homebrew or built from source)
#    - Git
#
#  Usage:
#    ./build-mac.sh [options]
#
#  Options:
#    -t, --type TYPE         Build type: Release or Debug (default: Release)
#    -l, --lowmem            Build low memory node (LOW_MEMORY_NODE=ON)
#    -n, --testnet           Build for testnet (BUILD_TESTNET=ON)
#    -s, --static            Build shared libraries OFF (static linking)
#    --no-lock-check         Disable chainbase lock checking
#    --skip-deps             Skip dependency installation
#    --install               Run make install after build
#    -j, --jobs N            Number of parallel jobs (default: hw.logicalcpu)
#    --boost-root PATH       Custom Boost installation path
#    --openssl-root PATH     Custom OpenSSL installation path
#    -h, --help              Show this help message
# ============================================================================

set -euo pipefail

# --- Defaults ---
BUILD_TYPE="Release"
LOW_MEMORY="OFF"
BUILD_TESTNET="OFF"
SHARED_LIBS="ON"
CHAINBASE_LOCK="ON"
SKIP_DEPS="false"
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
        -s|--static)
            SHARED_LIBS="OFF"; shift ;;
        --no-lock-check)
            CHAINBASE_LOCK="OFF"; shift ;;
        --skip-deps)
            SKIP_DEPS="true"; shift ;;
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
    JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 2)
fi

# --- Determine source directory ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR"

# --- Check we're in the right directory ---
if [[ ! -f "$SOURCE_DIR/CMakeLists.txt" ]]; then
    error "CMakeLists.txt not found in $SOURCE_DIR. Run this script from the viz-cpp-node root."
fi

# --- Verify macOS ---
if [[ "$(uname)" != "Darwin" ]]; then
    error "This script is for macOS only. Use build-linux.sh for Linux builds."
fi

# --- Check Xcode Command Line Tools ---
check_xcode() {
    if ! xcode-select -p &>/dev/null; then
        warn "Xcode Command Line Tools not found. Installing..."
        xcode-select --install
        error "Please re-run this script after Xcode Command Line Tools installation completes."
    fi
    info "Xcode Command Line Tools: $(xcode-select -p)"
}

# --- Check Homebrew ---
check_homebrew() {
    if ! command -v brew &>/dev/null; then
        error "Homebrew not found. Install it from https://brew.sh/ and re-run."
    fi
    info "Homebrew: $(brew --prefix)"
}

# --- Install dependencies via Homebrew ---
install_deps() {
    info "Installing build dependencies via Homebrew..."

    brew install \
        autoconf \
        automake \
        boost \
        cmake \
        git \
        libtool \
        openssl \
        python3 \
        readline

    # Optional: TCMalloc for LevelDB
    # brew install google-perftools

    info "Dependencies installed."
}

# --- Set OpenSSL path ---
detect_openssl() {
    if [[ -z "$OPENSSL_ROOT_ARG" ]]; then
        local openssl_prefix
        openssl_prefix=$(brew --prefix openssl 2>/dev/null || true)
        if [[ -n "$openssl_prefix" && -d "$openssl_prefix" ]]; then
            export OPENSSL_ROOT_DIR="$openssl_prefix"
            OPENSSL_ROOT_ARG="-DOPENSSL_ROOT_DIR=$openssl_prefix"
            info "Using Homebrew OpenSSL: $openssl_prefix"
        else
            warn "Could not detect Homebrew OpenSSL. Set --openssl-root or OPENSSL_ROOT_DIR manually."
        fi
    fi
}

# --- Setup environment ---
setup_env() {
    check_xcode
    check_homebrew

    if [[ "$SKIP_DEPS" == "false" ]]; then
        install_deps
    fi

    detect_openssl
}

setup_env

# --- Initialize submodules ---
if [[ -d "$SOURCE_DIR/.git" ]]; then
    info "Initializing git submodules..."
    git -C "$SOURCE_DIR" submodule update --init --recursive -f
fi

# --- Create build directory ---
BUILD_DIR="$SOURCE_DIR/build"
mkdir -p "$BUILD_DIR"

# --- Display configuration ---
echo ""
echo "============================================"
echo " VIZ macOS Build"
echo "============================================"
echo " Build Type:       $BUILD_TYPE"
echo " Low Memory Node:  $LOW_MEMORY"
echo " Build Testnet:    $BUILD_TESTNET"
echo " Shared Libs:      $SHARED_LIBS"
echo " Chainbase Locks:  $CHAINBASE_LOCK"
echo " Parallel Jobs:    $JOBS"
echo " Source Dir:       $SOURCE_DIR"
echo " Build Dir:        $BUILD_DIR"
if [[ -n "$BOOST_ROOT_ARG" ]]; then
echo " Boost:            $BOOST_ROOT_ARG"
else
echo " Boost:            (Homebrew default)"
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
    -DCHAINBASE_CHECK_LOCKING="$CHAINBASE_LOCK" \
    $BOOST_ROOT_ARG \
    $OPENSSL_ROOT_ARG

info "CMake configuration complete."

# --- Build ---
info "[2/3] Building with $JOBS parallel jobs..."
cmake --build "$BUILD_DIR" -j"$JOBS"

info "Build complete."

# --- Install ---
if [[ "$DO_INSTALL" == "true" ]]; then
    info "[3/3] Installing..."
    sudo cmake --install "$BUILD_DIR"
    info "Installation complete."
else
    info "[3/3] Skipping install (use --install to install)."
fi

echo ""
echo "============================================"
echo -e " ${GREEN}Build completed successfully!${NC}"
echo " Output directory: $BUILD_DIR"
echo "============================================"
