# Building VIZ

## Compile-Time Options (cmake)

### CMAKE_BUILD_TYPE=[Release/Debug]

Specifies whether to build with or without optimization and without or with
the symbol table for debugging. Unless you are specifically debugging or
running tests, it is recommended to build as release.

### LOW_MEMORY_NODE=[FALSE/TRUE]

Builds vizd to be a consensus-only low memory node. Data and fields not
needed for consensus are not stored in the object database.  This option is
recommended for witnesses and seed-nodes.

## Building under Docker

We ship Dockerfiles for building production and testnet images.

### Prerequisites

- Docker installed and running
- Docker Hub account (for pushing images)
- Git with submodules initialized

### Available Dockerfiles

| Dockerfile | Purpose | Image Tag | CMake Options |
|------------|---------|-----------|---------------|
| `share/vizd/docker/Dockerfile-production` | Mainnet node | `vizblockchain/vizd:latest` | `LOW_MEMORY_NODE=FALSE`, `ENABLE_MONGO_PLUGIN=FALSE` |
| `share/vizd/docker/Dockerfile-testnet` | Testnet node | `vizblockchain/vizd:testnet` | `BUILD_TESTNET=TRUE`, `LOW_MEMORY_NODE=FALSE` |
| `share/vizd/docker/Dockerfile-lowmem` | Low memory consensus node | `vizblockchain/vizd:lowmem` | `LOW_MEMORY_NODE=TRUE` |
| `share/vizd/docker/Dockerfile-mongo` | Node with MongoDB plugin | `vizblockchain/vizd:mongo` | `ENABLE_MONGO_PLUGIN=TRUE` |

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

Build the MongoDB-enabled image:

```bash
docker build -t vizblockchain/vizd:mongo -f share/vizd/docker/Dockerfile-mongo .
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

# MongoDB-enabled node
docker run -d \
  --name vizd-mongo \
  -p 8090:8090 -p 8091:8091 -p 2001:2001 \
  -v /path/to/blockchain:/var/lib/vizd \
  vizblockchain/vizd:mongo
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
    chain_test
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

## Building on Other Platforms

- The developers normally compile with GCC and Clang. These compilers should
  be well-supported on Unix-like systems.
- Community members occasionally attempt to compile the code with Intel and
  Microsoft compilers. These compilers may work, but the developers do not
  use them. Pull requests fixing warnings / errors from these compilers are
  accepted.
- The project requires **Boost 1.71+**, **CMake 3.16+**, and a **C++14**
  compiler. Ensure your platform meets these minimum requirements.
