#!/usr/bin/env bash
# ============================================================================
#  VIZ Linux Build Script
#
#  Prerequisites:
#    - Ubuntu 20.04+ (24.04 Noble recommended for system Boost 1.74)
#    - CMake 3.16+
#    - GCC 8+ or Clang 3.3+
#    - Boost 1.71+ (system packages or built from source)
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
#    --skip-deps             Skip dependency installation
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
        -m|--mongo)
            ENABLE_MONGO="ON"; shift ;;
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
    JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 2)
fi

# --- Determine source directory ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR"

# --- Check we're in the right directory ---
if [[ ! -f "$SOURCE_DIR/CMakeLists.txt" ]]; then
    error "CMakeLists.txt not found in $SOURCE_DIR. Run this script from the viz-cpp-node root."
fi

# --- Install dependencies ---
install_deps_ubuntu() {
    info "Installing build dependencies..."
    sudo apt-get update -qq

    sudo apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        autotools-dev \
        binutils \
        bsdmainutils \
        build-essential \
        cmake \
        git \
        ccache \
        libboost-chrono-dev \
        libboost-context-dev \
        libboost-coroutine-dev \
        libboost-date-time-dev \
        libboost-filesystem-dev \
        libboost-iostreams-dev \
        libboost-locale-dev \
        libboost-program-options-dev \
        libboost-serialization-dev \
        libboost-system-dev \
        libboost-test-dev \
        libboost-thread-dev \
        libbz2-dev \
        liblzma-dev \
        libzstd-dev \
        libreadline-dev \
        libssl-dev \
        libtool \
        ncurses-dev \
        pkg-config \
        zlib1g-dev

    info "Dependencies installed."
}

install_deps_fedora() {
    info "Installing build dependencies (Fedora)..."
    sudo dnf install -y \
        autoconf \
        automake \
        cmake \
        gcc-c++ \
        git \
        ccache \
        boost-devel \
        bzip2-devel \
        lzma-devel \
        libzstd-devel \
        readline-devel \
        openssl-devel \
        libtool \
        ncurses-devel \
        pkg-config \
        zlib-devel

    info "Dependencies installed."
}

if [[ "$SKIP_DEPS" == "false" ]]; then
    if command -v apt-get &>/dev/null; then
        install_deps_ubuntu
    elif command -v dnf &>/dev/null; then
        install_deps_fedora
    else
        warn "Unsupported package manager. Please install dependencies manually."
        warn "See documentation/building.md for required packages."
    fi
fi

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
echo " VIZ Linux Build"
echo "============================================"
echo " Build Type:       $BUILD_TYPE"
echo " Low Memory Node:  $LOW_MEMORY"
echo " Build Testnet:    $BUILD_TESTNET"
echo " Enable MongoDB:   $ENABLE_MONGO"
echo " Shared Libs:      $SHARED_LIBS"
echo " Chainbase Locks:  $CHAINBASE_LOCK"
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
