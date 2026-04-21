#!/usr/bin/env bash
# ============================================================================
#  VIZ Linux Dependency Installer
#
#  Installs all build dependencies required for viz-cpp-node.
#  Must be run with root privileges (sudo).
#
#  Usage:
#    sudo ./install-deps-linux.sh
#
#  Supported distros:
#    - Ubuntu / Debian (apt-get)
#    - Fedora / RHEL   (dnf)
# ============================================================================

set -euo pipefail

# --- Colors ---
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# --- Require root ---
if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root: sudo $0"
fi

# --- Install Ubuntu/Debian deps ---
install_deps_ubuntu() {
    info "Installing build dependencies (Ubuntu/Debian)..."
    apt-get update -qq

    apt-get install -y --no-install-recommends \
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

    info "Dependencies installed successfully."
}

# --- Install Fedora/RHEL deps ---
install_deps_fedora() {
    info "Installing build dependencies (Fedora/RHEL)..."
    dnf install -y \
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

    info "Dependencies installed successfully."
}

# --- Dispatch ---
if command -v apt-get &>/dev/null; then
    install_deps_ubuntu
elif command -v dnf &>/dev/null; then
    install_deps_fedora
else
    warn "Unsupported package manager. Please install dependencies manually."
    warn "See documentation/building.md for the required package list."
    exit 1
fi

echo ""
echo "============================================"
echo -e " ${GREEN}Dependency installation complete!${NC}"
echo " Now run: ./build-linux.sh"
echo "============================================"
