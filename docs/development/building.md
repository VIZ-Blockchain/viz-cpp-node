# Building

VIZ Ledger node uses CMake 3.16+ and requires Boost 1.71+ with the coroutine component. Supported platforms: Ubuntu 24.04+, macOS (Homebrew), Windows (MSVC or MinGW).

---

## Linux (Ubuntu/Debian)

### Step 1: Install dependencies (requires root)

```bash
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

This installs: CMake, GCC/G++, Git, Make, ccache, OpenSSL, Boost 1.71 (all required components including coroutine/context), readline, and compression libraries.

### Step 2: Build (run as regular user, not root)

```bash
chmod +x build-linux.sh
./build-linux.sh
```

**Common options:**

```bash
./build-linux.sh              # Release build (default)
./build-linux.sh -n           # Testnet build
./build-linux.sh -t Debug -j4 # Debug build with 4 parallel jobs
./build-linux.sh --install    # Install to system after build

# Custom dependency paths
./build-linux.sh --boost-root /opt/boost_1_74_0 --openssl-root /opt/openssl
```

### Fedora/RHEL

The same scripts detect `dnf` automatically. Packages installed: `cmake`, `gcc-c++`, `git`, `ccache`, `boost-devel`, `openssl-devel`, `bzip2-devel`, `zstd-devel`.

---

## macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

Requires Xcode Command Line Tools and Homebrew. The script installs: `boost`, `cmake`, `git`, `autoconf`, `automake`, `libtool`, `openssl`, `readline`.

**Options:**

```bash
./build-mac.sh -n              # Testnet
./build-mac.sh --skip-deps     # Skip Homebrew installs
./build-mac.sh --boost-root /opt/boost_1_74_0
```

Apple Silicon builds natively: the tree contains no x86-only code (the SSE-only `equihash` that fc once vendored was unused and has been removed), and CMake adds the Homebrew library directories to the link path so the static Boost.Iostreams dependencies (`zstd`, `lzma`, `icu*`) resolve. A cold `vizd` build takes about 10 minutes on an 8-core M-series laptop, against roughly an hour for the same build under amd64 emulation in Docker.

### Where compile time goes

Measured with clang `-ftime-trace` on a Release build. Five translation units account for a third of the project's compile time: `libraries/chain/database.cpp` (~250 s), `libraries/wallet/wallet.cpp`, `libraries/network/dlt_p2p_node.cpp`, `libraries/protocol/operations.cpp` and `plugins/snapshot/plugin.cpp` (100–160 s each). In a typical unit about two thirds of the time is the optimizer, not the parser: every file that serializes a `signed_block` instantiates the `fc::raw` pack/unpack visitors for every field of every operation (about 1,500 functions), and `-O3` then optimizes each of them again. Precompiled headers (`-DENABLE_PCH=ON`) therefore shave only the parsing third; `ccache` is what makes rebuilds fast.

---

## Windows (MinGW)

Set required environment variables, then run the batch script:

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

**Optional variables:**

| Variable | Default | Description |
|----------|---------|-------------|
| `VIZ_BUILD_TYPE` | Release | Release or Debug |
| `VIZ_BUILD_TESTNET` | OFF | Testnet build |
| `VIZ_FULL_STATIC` | OFF | Fully static binary |
| `VIZ_CMAKE_EXTRA` | — | Additional CMake flags |

**Requirements:** MinGW-w64 with C++11 and SSE4.2, CMake 3.16+, Boost 1.71+ (static, `link=static threading=multi runtime-link=shared`), OpenSSL for Windows.

---

## Windows (MSVC)

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-msvc.bat
```

**Optional variables:**

| Variable | Default | Description |
|----------|---------|-------------|
| `VIZ_VS_VERSION` | "Visual Studio 17 2022" | Visual Studio generator |
| `VIZ_BUILD_TYPE` | Release | Build type |
| `VIZ_BUILD_TESTNET` | OFF | Testnet build |

**Requirements:** Visual Studio 2019+ with "Desktop development with C++" workload, CMake 3.16+.

---

## Docker

The repository ships Dockerfiles for multiple configurations:

| Dockerfile | Description |
|-----------|-------------|
| `Dockerfile-production` | Full mainnet node (Release) |
| `Dockerfile-testnet` | Testnet (`BUILD_TESTNET=ON`) |

All Dockerfiles use a two-stage build to minimize image size and use Boost 1.71 packages (`libboost-coroutine-dev`, `libboost-context-dev`).

---

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTNET` | OFF | Build for testnet |
| `CHAINBASE_CHECK_LOCKING` | OFF | Enable lock checking (development only) |
| `BUILD_SHARED_LIBRARIES` | OFF | Build shared libraries |
| `USE_PCH` | OFF | Enable precompiled headers (faster rebuilds) |

---

## Advanced: `configure_build.py`

Wraps CMake with sensible defaults and cross-compilation support:

```bash
# Release build
python3 programs/build_helpers/configure_build.py --release --src ../..

# Cross-compile for Windows with MinGW
python3 programs/build_helpers/configure_build.py --win --release

# Custom dependency paths
python3 programs/build_helpers/configure_build.py \
  --boost-dir /opt/boost_1_74_0 \
  --openssl-dir /opt/openssl \
  --release
```

---

## New Plugin Scaffolding

```bash
python3 programs/util/newplugin.py graphene myplugin
```

Generates: `CMakeLists.txt`, plugin header/implementation, API header/implementation under `libraries/plugins/myplugin/`.

---

## Build Targets

| Binary | Description |
|--------|-------------|
| `vizd` | Main node daemon |
| `cli_wallet` | Command-line wallet |
| `js_operation_serializer` | JavaScript operation serializer |
| `size_checker` | Size analysis utility |

---

## Troubleshooting

**Boost version below 1.71:** Install Boost 1.71+ from your package manager (Ubuntu 24.04 ships 1.74). On macOS, `brew install boost` provides a recent version. On Windows, build from source with the coroutine component.

**`Do not run this script as root` error:** Use `sudo ./install-deps-linux.sh` for dependencies, then run `./build-linux.sh` as a regular user.

**Coroutine component missing:** Ensure `libboost-coroutine-dev` and `libboost-context-dev` are installed on Ubuntu/Debian.

**macOS OpenSSL not found:** Set `OPENSSL_ROOT_DIR` manually: `export OPENSSL_ROOT_DIR=$(brew --prefix openssl)`.

**Windows MinGW missing variables:** Both `BOOST_ROOT` and `OPENSSL_ROOT_DIR` must be set before running `build-mingw.bat`.

---

See also: [Plugin Development](./plugin-development.md), [Testing](./testing.md), [Plugins Overview](../plugins/overview.md).
