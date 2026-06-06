# Building VIZ

## Compile-Time Options (cmake)

### CMAKE_BUILD_TYPE=[Release/Debug]

Specifies whether to build with or without optimization and without or with
the symbol table for debugging. Unless you are specifically debugging or
running tests, it is recommended to build as release.

### LOW_MEMORY_NODE=[FALSE/TRUE]

Builds vizd to be a consensus-only low memory node. Data and fields not
needed for consensus are not stored in the object database.  This option is
recommended for validators and seed-nodes.

## Building under Docker

We ship Dockerfiles for building production and testnet images.

### Prerequisites

- Docker installed and running
- Docker Hub account (for pushing images)
- Git with submodules initialized

### Available Dockerfiles

| Dockerfile | Purpose | Image Tag | CMake Options |
|------------|---------|-----------|---------------|
| `share/vizd/docker/Dockerfile-production` | Mainnet node | `vizblockchain/vizd:latest` | `LOW_MEMORY_NODE=FALSE` |
| `share/vizd/docker/Dockerfile-testnet` | Testnet node | `vizblockchain/vizd:testnet` | `BUILD_TESTNET=TRUE`, `LOW_MEMORY_NODE=FALSE` |
| `share/vizd/docker/Dockerfile-lowmem` | Low memory consensus node | `vizblockchain/vizd:lowmem` | `LOW_MEMORY_NODE=TRUE` |

### Building Locally

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
```

If you already cloned without `--recursive`, initialize submodules:

```bash
git submodule update --init --recursive
```

Build the production image:

```bash
docker build -t vizblockchain/vizd:latest -f share/vizd/docker/Dockerfile-production .
```

Build the testnet image:

```bash
docker build -t vizblockchain/vizd:testnet -f share/vizd/docker/Dockerfile-testnet .
```

Build the low-memory image (for witnesses and seed-nodes):

```bash
docker build -t vizblockchain/vizd:lowmem -f share/vizd/docker/Dockerfile-lowmem .
```

### Pushing to Docker Hub

1. **Login to Docker Hub:**

```bash
docker login
```

Enter your Docker Hub username and password when prompted.

2. **Tag the image (if using a different local tag):**

```bash
docker tag vizblockchain/vizd:latest YOUR_USERNAME/vizd:latest
```

3. **Push the image:**

```bash
# Push to official VIZ repository (requires access)
docker push vizblockchain/vizd:latest

# Or push to your personal repository
docker push YOUR_USERNAME/vizd:latest
```

### Building Specific Versions

To build a specific version or branch:

```bash
git checkout v1.2.3  # or any tag/branch
git submodule update --init --recursive
docker build -t vizblockchain/vizd:1.2.3 -f share/vizd/docker/Dockerfile-production .
```

### Running the Container

```bash
# Production node
docker run -d \
  --name vizd \
  -p 8090:8090 -p 8091:8091 -p 2001:2001 \
  -v /path/to/blockchain:/var/lib/vizd \
  vizblockchain/vizd:latest

# Testnet node
docker run -d \
  --name vizd-testnet \
  -p 8090:8090 -p 8091:8091 -p 2001:2001 \
  -v /path/to/testnet-data:/var/lib/vizd \
  vizblockchain/vizd:testnet

# Low-memory node (witness/seed node)
docker run -d \
  --name vizd-lowmem \
  -p 8090:8090 -p 8091:8091 -p 2001:2001 \
  -v /path/to/blockchain:/var/lib/vizd \
  vizblockchain/vizd:lowmem

```

### Troubleshooting

**Mirror sync errors during apt-get:**
Ubuntu mirrors occasionally fail during sync. The Dockerfile includes retry logic, but if it persists, re-run the build:

```bash
docker build --no-cache -t vizblockchain/vizd:latest -f share/vizd/docker/Dockerfile-production .
```

**Submodule issues:**
Ensure submodules are properly initialized:

```bash
git submodule deinit -f .
git submodule update --init --recursive
```

**GCC alignment errors:**
If you see `size of array element is not a multiple of its alignment`, ensure you're using the latest code which fixes GCC 12+ compatibility.

**Dockerfile Base Image:**
All Dockerfiles use `phusion/baseimage:noble-1.0.3` (Ubuntu 24.04 Noble) as the base image. This provides GCC 12+ and modern dependencies required for building VIZ.

**Build Stages:**
All Dockerfiles use multi-stage builds:
- **Builder stage**: Compiles the VIZ node with all dependencies
- **Production stage**: Minimal runtime image with only the compiled binaries

**Exposed Ports:**
All Docker images expose the following ports:
- `8090` - HTTP RPC service
- `8091` - WebSocket RPC service
- `2001` - P2P network service

**Volumes:**
All Docker images define two volumes:
- `/var/lib/vizd` - Blockchain data directory
- `/etc/vizd` - Configuration directory


## Building on Ubuntu 24.04 (without Docker)

VIZ requires **Boost 1.71 or later** and a C++14-capable compiler (GCC 8+).
Ubuntu 24.04 (Noble) provides Boost 1.74 from the package manager, which meets
this requirement.

### Quick Build (using build scripts)

The build is split into two scripts so that dependency installation (which
requires root) is kept separate from the build (which must run as a regular
user):

**Step 1 — install system dependencies (once, as root):**

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
    cd viz-cpp-node
    chmod +x install-deps-linux.sh build-linux.sh
    sudo ./install-deps-linux.sh

**Step 2 — configure and build (as regular user):**

    ./build-linux.sh

> **Note:** `build-linux.sh` refuses to run as root. Always run it as your
> regular user account after installing dependencies.

Common options:

    ./build-linux.sh -l                  # Low memory node (witness/seed)
    ./build-linux.sh -n                  # Testnet build
    ./build-linux.sh -t Debug            # Debug build
    ./build-linux.sh -j 4                # Limit to 4 parallel jobs
    ./build-linux.sh --clean             # Remove build dir before configuring
    ./build-linux.sh --install           # Build and install to /usr/local
    ./build-linux.sh --boost-root /opt/boost_1_74_0  # Custom Boost path

Run `./build-linux.sh -h` for full usage information.

### Manual Build

    # Required packages
    sudo apt-get install -y \
        autoconf \
        automake \
        cmake \
        g++ \
        git \
        libssl-dev \
        libtool \
        make \
        pkg-config

    # Boost packages (also required)
    sudo apt-get install -y \
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
        libboost-thread-dev

    # Compression libraries (required for Boost.Iostreams)
    sudo apt-get install -y \
        libbz2-dev \
        liblzma-dev \
        libzstd-dev \
        zlib1g-dev

    # Optional packages (not required, but will make a nicer experience)
    sudo apt-get install -y \
        libncurses5-dev \
        libreadline-dev \
        perl

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
    cd viz-cpp-node
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DBoost_NO_BOOST_CMAKE=ON ..
    make -j$(nproc) vizd
    make -j$(nproc) cli_wallet
    # optional
    sudo make install  # defaults to /usr/local

### Building on Older Ubuntu Versions

Ubuntu versions older than 24.04 ship Boost versions below 1.71 in their
package managers, which does not satisfy the project's requirement. If you
need to build on an older Ubuntu, you must compile Boost 1.71+ from source:

    BOOST_ROOT=$HOME/opt/boost_1_74_0
    wget https://boostorg.jfrog.io/artifactory/main/release/1.74.0/source/boost_1_74_0.tar.bz2
    tar xjf boost_1_74_0.tar.bz2
    cd boost_1_74_0
    ./bootstrap.sh --prefix=$BOOST_ROOT
    ./b2 -j$(nproc) install

Then build VIZ pointing to the custom Boost installation:

    cmake -DCMAKE_BUILD_TYPE=Release -DBoost_NO_BOOST_CMAKE=ON -DBOOST_ROOT=$BOOST_ROOT ..
    make -j$(nproc) vizd
    make -j$(nproc) cli_wallet

## Building on macOS

VIZ requires **Boost 1.71 or later** and a C++14-capable compiler.
macOS uses Clang (via Xcode) with `libc++` as the standard library.

### Prerequisites

- macOS 12 (Monterey) or later
- Xcode Command Line Tools
- Homebrew package manager

### Install Xcode Command Line Tools

Install Xcode and its command line tools. In macOS 10.14 (Mojave)
and newer, you will be prompted to install developer tools when running a
developer command in the terminal. Alternatively:

    xcode-select --install

Accept the Xcode license if you have not already:

    sudo xcodebuild -license accept

### Install Homebrew

Install Homebrew by following the instructions here: https://brew.sh/

### Install VIZ Dependencies

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

Homebrew provides a recent version of Boost (1.74+) which satisfies the
Boost 1.71 minimum requirement.

*Optional.* To use TCMalloc in LevelDB:

    brew install google-perftools

### Quick Build (using build script)

The `build-mac.sh` script handles Xcode/Homebrew checks, dependency
installation, OpenSSL path detection, submodule initialization, CMake
configuration, and building automatically:

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node.git
    cd viz-cpp-node
    chmod +x build-mac.sh
    ./build-mac.sh

Common options:

    ./build-mac.sh -l                  # Low memory node (witness/seed)
    ./build-mac.sh -n                  # Testnet build
    ./build-mac.sh -t Debug            # Debug build
    ./build-mac.sh -j 4               # Limit to 4 parallel jobs
    ./build-mac.sh --install            # Build and install to /usr/local
    ./build-mac.sh --boost-root /opt/boost_1_74_0  # Custom Boost path
    ./build-mac.sh --skip-deps          # Skip brew install

Run `./build-mac.sh -h` for full usage information.

### Manual Build

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node.git
    cd viz-cpp-node
    export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make -j$(sysctl -n hw.logicalcpu)

Also, some useful build targets for `make` are:

    vizd
    cli_wallet

e.g.:

    make -j$(sysctl -n hw.logicalcpu) vizd

This will only build `vizd`.

### macOS Notes

- **OpenSSL**: macOS ships an outdated version of OpenSSL. The Homebrew
  installation is required, and `OPENSSL_ROOT_DIR` must be exported before
  running CMake.
- **ZLIB**: On macOS, zlib is linked automatically because the Homebrew
  OpenSSL static libraries have a dependency on it.
- **readline**: Optional but recommended for `cli_wallet` interactive use.
  If not found, the build proceeds without it.
- **libc++**: The build system uses `libc++` (the macOS standard library)
  with C++14. Do not attempt to use `libstdc++`.

## Building on Windows

VIZ supports building on Windows with **MSVC** (Visual Studio) or **MinGW**.
The build system requires **Boost 1.71 or later** with static linking.

### Prerequisites

- Windows 10 or later
- CMake 3.16 or later
- Git for Windows
- Boost 1.71+ (built from source or prebuilt binaries)
- OpenSSL for Windows

### Windows Dependencies Download Links

All external libraries required for building VIZ on Windows are listed below.
Download and install each one before proceeding.

| Dependency | Minimum Version | Recommended Version | Download URL |
|------------|----------------|---------------------|---------------|
| Visual Studio (MSVC) | 2019 | 2019 or 2022 | https://visualstudio.microsoft.com/downloads/ |
| CMake | 3.16 | Latest 3.x | https://cmake.org/download/ |
| Git for Windows | Any | Latest | https://git-scm.com/download/win |
| Boost | 1.71 | 1.84.0 | https://www.boost.org/users/download/ |
| OpenSSL | 1.1.1 | 3.0.x | https://slproweb.com/products/Win32OpenSSL.html |
| Perl (OpenSSL source build) | 5.20 | Strawberry Perl 5.40 | https://strawberryperl.com/ |
| NASM (OpenSSL source build) | 2.13 | Latest 2.x | https://www.nasm.us/ |
| MinGW-w64 (MinGW builds only) | Any recent | Via MSYS2 | https://www.msys2.org/ |

#### Visual Studio

Download from https://visualstudio.microsoft.com/downloads/ (the free Community
edition is sufficient). During installation, select the **Desktop development
with C++** workload.

Tested generators:

- `Visual Studio 16 2019` (VS 2019, MSVC 19.29)
- `Visual Studio 17 2022` (VS 2022)

#### CMake

Download from https://cmake.org/download/. Make sure `cmake` is in your
`PATH` (the installer offers to add it). Alternatively, install the
**CMake tools for Windows** component from the Visual Studio installer.

#### Git

Download from https://git-scm.com/download/win. A standard installation
is sufficient.

#### Boost

Download the source archive from https://www.boost.org/users/download/
(Boost 1.84.0 recommended). Build from source with MSVC:

    :: Using VS 2019 (Developer Command Prompt)
    cd boost_1_84_0
    bootstrap.bat
    b2 -j%NUMBER_OF_PROCESSORS% variant=release link=static threading=multi runtime-link=shared install --prefix=D:\Boost

For MinGW builds, replace the toolset:

    b2 -j%NUMBER_OF_PROCESSORS% toolset=gcc variant=release link=static threading=multi install --prefix=D:\Boost

After building, set the environment variable:

    setx BOOST_ROOT "D:\Boost"

#### OpenSSL

**Option A — Prebuilt binaries (recommended):**

Download the full Win64 installer from
https://slproweb.com/products/Win32OpenSSL.html (not the "Light" version).
Set the environment variable after installation:

    setx OPENSSL_ROOT_DIR "C:\OpenSSL-Win64"

**Option B — Build from source:**

If you need a specific version or static libraries, build from source.
This requires **Perl** (Strawberry Perl from https://strawberryperl.com/)
and **NASM** (from https://www.nasm.us/):

    :: Using VS 2019 (Developer Command Prompt)
    cd openssl-3.0.16
    perl Configure VC-WIN64A no-shared --prefix=D:\OpenSSL --openssldir=D:\OpenSSL\ssl
    nmake
    nmake install

After building, set the environment variable:

    setx OPENSSL_ROOT_DIR "D:\OpenSSL"

#### Perl and NASM (only needed for building OpenSSL from source)

- **Perl**: Install Strawberry Perl from https://strawberryperl.com/. ActiveState
  Perl (https://activestate.com/products/activeperl/) also works.
- **NASM**: Download from https://www.nasm.us/ and add it to your `PATH`.

#### MinGW-w64 (only for MinGW builds)

The easiest way to get MinGW-w64 is via **MSYS2**
(https://www.msys2.org/). After installing MSYS2, run:

    pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make

Add the MSYS2 `mingw64/bin` directory to your system `PATH`.

### Using Visual Studio (MSVC)

#### 1. Install Visual Studio

Install Visual Studio 2019 or later with the **Desktop development with C++**
workload. This provides the MSVC compiler and Windows SDK.

#### 2. Install CMake

Download and install CMake from https://cmake.org/download/ or install via
the Visual Studio CMake workload.

#### 3. Install Boost

Download Boost source from https://www.boost.org/users/download/ and build
it from source (Boost 1.74 recommended):

    :: Download and extract boost_1_74_0.tar.bz2
    cd boost_1_74_0
    bootstrap.bat
    b2 -j%NUMBER_OF_PROCESSORS% variant=release link=static threading=multi runtime-link=shared install

Set the `BOOST_ROOT` environment variable to point to your Boost installation:

    setx BOOST_ROOT "C:\Boost" :: adjust to your install path

#### 4. Install OpenSSL

Download prebuilt OpenSSL for Windows from https://slproweb.com/products/Win32OpenSSL.html
(the full install, not the "Light" version). Set `OPENSSL_ROOT_DIR`:

    setx OPENSSL_ROOT_DIR "C:\OpenSSL-Win64"

#### 5. Clone and Build

Open a **Developer Command Prompt for VS** and run:

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node.git
    cd viz-cpp-node

**Using the build script** (recommended):

    build-msvc.bat

This script reads `BOOST_ROOT` and `OPENSSL_ROOT_DIR` from environment
variables and runs CMake configure + build automatically. See the script
header for available options like `VIZ_BUILD_TYPE`, `VIZ_LOW_MEMORY`,
`VIZ_BUILD_TESTNET`, and `VIZ_VS_VERSION`.

**Manual build**:

    mkdir build && cd build
    cmake -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=%BOOST_ROOT% -DOPENSSL_ROOT_DIR=%OPENSSL_ROOT_DIR% ..
    cmake --build . --config Release

This will build `vizd.exe` and `cli_wallet.exe` in `build/programs/`.

### Using MinGW

MinGW builds are also supported.

**Using the build script** (recommended):

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node.git
    cd viz-cpp-node
    build-mingw.bat

This script reads `BOOST_ROOT` and `OPENSSL_ROOT_DIR` from environment
variables and runs CMake configure + build automatically. See the script
header for available options like `VIZ_BUILD_TYPE`, `VIZ_LOW_MEMORY`,
`VIZ_BUILD_TESTNET`, and `VIZ_FULL_STATIC`.

**Using the Python helper** for cross-compilation from Linux:

    python programs/build_helpers/configure_build.py --win --release

**Manual build**:

    git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node.git
    cd viz-cpp-node
    mkdir build && cd build
    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=%BOOST_ROOT% -DOPENSSL_ROOT_DIR=%OPENSSL_ROOT_DIR% ..
    mingw32-make -j%NUMBER_OF_PROCESSORS%

### Windows Notes

- **Boost linkage**: The build system forces static Boost linkage on Windows
  (`BOOST_ALL_DYN_LINK=OFF`). Make sure you built Boost with `link=static`.
- **MSVC warnings**: The build suppresses specific MSVC warnings (C4503,
  C4267, C4244) and disables Safe Exception Handler emission (`/SAFESEH:NO`).
- **MinGW Debug builds**: Use `-O2` optimization to avoid "File too big"
  assembler errors. Release builds use `-O3`.
- **MinGW requires**: `-Wa,-mbig-obj` flag for large object file support.
- **FULL_STATIC_BUILD**: Set this CMake option to produce fully static
  executables (links static libstdc++ and libgcc with MinGW).
## Building on Windows with MinGW UCRT64 (fully static, runs on Win7+)

This method produces a fully static `vizd.exe` with no external DLL
dependencies beyond what Windows itself provides (kernel32, ntdll, ws2_32,
bcrypt). The binary runs on Windows 7 SP1 x64 and later without installing
any runtime redistributables.

**Why UCRT64 and not MINGW64?** UCRT (Universal C Runtime) is the modern
replacement for MSVCRT. It is available on Win7/8/8.1 via Windows Update
(KB2999226) and built-in from Win10 onward. The UCRT64 toolchain can link
UCRT statically, producing a binary that is self-contained and
behaviorally close to a Linux build.

**Why `fcontext` matters for Boost.Context/fibers?** VIZ uses
Boost.Coroutine2 which relies on Boost.Context for fiber/coroutine
switching. On Win64 MinGW there are three possible backends:

| Backend | Problem |
|---|---|
| `winfibers` | Requires explicit thread conversion via Win32 API; crashes if a thread was not converted |
| `ucontext` | Does not save XMM6–XMM15 registers (non-volatile in the Win64 ABI) — guaranteed crash with SSE2/AVX code |
| `fcontext` | Pure ASM, saves TIB pointers and all non-volatile XMM registers, 16-byte stack alignment — stable |

Boost must be compiled with `context-impl=fcontext` explicitly.

### Prerequisites

- Windows 7 SP1 x64 or later (build machine: Windows 10/11 recommended)
- MSYS2 installed to `C:\msys64` — https://www.msys2.org/
- CMake 3.16 or later (the MSYS2 package is used below)
- ~5 GB free disk space for dependencies and build artifacts

All commands below are run inside the **MSYS2 UCRT64** terminal
(not MINGW64, not the plain MSYS terminal).

### ⚠️ NTP Note
To avoid issues related to time synchronization and NTP offsets, **do not use `w32tm`** (the default Windows time utility).  
Instead, it is recommended to install and run **Meinberg NTP for Windows**, available here:  
[https://www.meinbergglobal.com/english/sw/ntp.htm#ntp_stable](https://www.meinbergglobal.com/english/sw/ntp.htm#ntp_stable)

Meinberg provides a full NTP service implementation compatible with Unix systems, ensuring more accurate and stable local clock synchronization — which is critical for running the blockchain node correctly.

### Step 1 — Install toolchain packages

```bash
pacman -Syu
# Restart the terminal if pacman asks, then run again:
pacman -Syu

pacman -S \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-perl \
  mingw-w64-ucrt-x86_64-nasm \
  autoconf \
  automake \
  libtool \
  make \
  git \
  wget
```

Create a `make` alias so autoconf test scripts can find it:

```bash
ln -s /ucrt64/bin/mingw32-make.exe /usr/bin/make
```

### Step 2 — Build Boost 1.84 (static, fcontext)

```bash
cd ~
wget https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.bz2
tar xjf boost_1_84_0.tar.bz2
cd boost_1_84_0

./bootstrap.sh --with-toolset=gcc

./b2 -j4 \
<<<<<<< HEAD
  toolset=gcc \
  address-model=64 \
  variant=release \
  link=static \
  threading=multi \
  runtime-link=static \
  context-impl=fcontext \
  define=_WIN32_WINNT=0x0601 \
  define=WINVER=0x0601 \
  cxxflags="-march=x86-64 -mtune=generic" \
  cflags="-march=x86-64 -mtune=generic" \
  --with-atomic \
  --with-chrono \
  --with-context \
  --with-coroutine \
  --with-date_time \
  --with-filesystem \
  --with-iostreams \
  --with-locale \
  --with-program_options \
  --with-regex \
  --with-serialization \
  --with-system \
  --with-test \
  --with-thread \
  install --prefix=/c/Boost

=======
    toolset=gcc \
    address-model=64 \
    variant=release \
    link=static \
    threading=multi \
    runtime-link=static \
    context-impl=fcontext \
    define=_WIN32_WINNT=0x0601 \
    define=WINVER=0x0601 \
    --with-atomic \
    --with-chrono \
    --with-context \
    --with-coroutine \
    --with-date_time \
    --with-filesystem \
    --with-iostreams \
    --with-locale \
    --with-program_options \
    --with-regex \
    --with-serialization \
    --with-system \
    --with-test \
    --with-thread \
    install --prefix=/c/Boost
>>>>>>> 8086b055 (build: update CMake configuration for MinGW UCRT64 and add Windows CI workflow)
```

### Step 3 — Build OpenSSL 3.0 (static, mingw64 target)

```bash
cd ~
wget https://github.com/openssl/openssl/releases/download/openssl-3.0.16/openssl-3.0.16.tar.gz
tar xzf openssl-3.0.16.tar.gz
cd openssl-3.0.16

# Use MSYS2's Perl — NOT Strawberry/ActivePerl from Windows PATH
/usr/bin/perl Configure mingw64 no-shared \
    --prefix=/c/OpenSSL \
    --openssldir=/c/OpenSSL/ssl \
    -D_WIN32_WINNT=0x0601

make -j4
make install_sw
make install_ssldirs
```

> If you have Strawberry Perl or ActivePerl installed on Windows, make sure
> `/usr/bin/perl` is used, not the Windows one. You can verify with:
> `perl -e 'print $^O'` — it must print `msys`, not `MSWin32`.

Note: In some cases, OpenSSL searches for openssl.cnf directly in the root installation directory rather than in the ssl subdirectory. To prevent build or runtime issues, it is safer to copy the configuration file immediately after installation:

```bash
cp ./apps/openssl.cnf /c/OpenSSL/
```

### Step 4 — Clone the repository

```bash
cd ~
git clone --recursive https://github.com/m0ssa99/viz-cpp-node.git -b windows_suport
cd viz-cpp-node
```

If you already cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

Pre-generate the secp256k1 configure script (required on Windows because
the autoconf step runs outside of CMake):

```bash
cd thirdparty/fc/vendor/secp256k1-zkp
autoreconf -fi
cd ~/viz-cpp-node
```

### Step 5 — Configure and build

```bash
mkdir build && cd build

cmake -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_STANDARD=14 \
    -DCMAKE_CXX_STANDARD_REQUIRED=ON \
    -DBOOST_ROOT="/c/Boost" \
    -DBOOST_INCLUDEDIR="/c/Boost/include/boost-1_84" \
    -DBOOST_LIBRARYDIR="/c/Boost/lib" \
    -DBoost_NO_BOOST_CMAKE=ON \
    -DBoost_NO_SYSTEM_PATHS=ON \
    -DBoost_USE_STATIC_LIBS=ON \
    -DBoost_USE_STATIC_RUNTIME=ON \
    -DBoost_USE_MULTITHREADED=ON \
    -DBoost_ARCHITECTURE="-x64" \
    -DBoost_COMPILER="-mgw16" \
    -DOPENSSL_ROOT_DIR="/c/OpenSSL" \
    -DOPENSSL_USE_STATIC_LIBS=ON \
    -DFULL_STATIC_BUILD=ON \
    -DCMAKE_C_FLAGS="-DWINVER=0x0601 -DSECP256K1_STATIC" \
    -DCMAKE_CXX_FLAGS="-DWINVER=0x0601 -DSECP256K1_STATIC -Wa,-mbig-obj" \
<<<<<<< HEAD
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,--start-group -lbcrypt -Wl,--end-group" \
=======
    -DCMAKE_EXE_LINKER_FLAGS="-static" \
>>>>>>> 8086b055 (build: update CMake configuration for MinGW UCRT64 and add Windows CI workflow)
    ..

mingw32-make -j4 vizd
mingw32-make -j4 cli_wallet
```

The binaries will be in `build/programs/vizd/vizd.exe` and
`build/programs/cli_wallet/cli_wallet.exe`.

### Step 6 — Verify no unexpected DLL dependencies

```bash
objdump -p programs/vizd/vizd.exe | grep "DLL Name"
```

Expected output — only Windows system DLLs, all present on Win7+:

```
DLL Name: KERNEL32.dll
DLL Name: ntdll.dll
DLL Name: WS2_32.dll
DLL Name: IPHLPAPI.dll
DLL Name: bcrypt.dll
```

If you see any `libstdc++`, `libgcc`, `libwinpthread`, or `msvcrt` entries,
`FULL_STATIC_BUILD` was not applied correctly — recheck the cmake flags.

### Notes

- **`Boost_COMPILER=-mgw16`**: the GCC 16 toolchain in MSYS2 UCRT64 tags
  its libraries with `-mgw16-`. If you use a different GCC version the
  suffix will differ (e.g. `-mgw14-` for GCC 14). Check with
  `ls /c/Boost/lib/ | head -5` and adjust accordingly.
- **`SECP256K1_STATIC`**: without this define, the secp256k1 headers emit
  `__declspec(dllimport)` on all symbols, causing link errors with
  `__imp_` prefixed undefined references even though you have a static
  library.
- **`bcrypt.dll`**: present on all Windows versions from Vista onward.
  Boost.Filesystem uses BCrypt for `unique_path()`. It is a system DLL —
  no redistribution needed.
- **`-Wa,-mbig-obj`**: MinGW on Win64 generates large `.o` files due to
  C++ template instantiations. Without this flag you get "File too big"
  assembler errors.
- **`autoreconf -fi`** on secp256k1: the submodule ships only
  `configure.ac`, not the generated `configure` script. On Linux/macOS,
  the CMake ExternalProject autogen step handles this automatically. On
  Windows it must be run once manually before the first build.
<<<<<<< HEAD

### Quick Build Automation with `build_mingw.sh`

For convenience, you can automate the build process using the `build_mingw.sh` helper script located in the repository root. This script handles the entire toolchain setup, dependency compilation (Boost, OpenSSL), and the node build itself.

**Prerequisites:**

1. Download and install **MSYS2/MinGW** from https://www.msys2.org/.
2. Launch the **MinGW UCRT64** terminal and update the package system:
   ```bash
   pacman -Syu
   ```
   (Confirm any terminal restart requests and run the command again if necessary.)

**Instructions:**

1. **Copy the helper script:**
   Copy `build_mingw.sh` from the VIZ repository root into your MSYS2 home directory (usually `C:\msys64\home\<username>`).

   ```bash
   # From your MSYS2 home directory:
   cp /path/to/viz-cpp-node/build_mingw.sh .
   ```

2. **Run the automation:**
   Open the **MinGW UCRT64** terminal and execute:

   ```bash
   cd ~
   chmod +x build_mingw.sh
   ./build_mingw.sh
   ```

This script reproduces the entire build process with a single command, producing fully static binaries without manual step execution.
=======
>>>>>>> 8086b055 (build: update CMake configuration for MinGW UCRT64 and add Windows CI workflow)

## Building on Other Platforms

- The developers normally compile with GCC and Clang. These compilers should
  be well-supported on Unix-like systems.
- Community members occasionally attempt to compile the code with Intel and
  Microsoft compilers. These compilers may work, but the developers do not
  use them. Pull requests fixing warnings / errors from these compilers are
  accepted.
- The project requires **Boost 1.71+**, **CMake 3.16+**, and a **C++14**
  compiler. Ensure your platform meets these minimum requirements.