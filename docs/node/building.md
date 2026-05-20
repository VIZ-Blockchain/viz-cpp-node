# Building from Source

VIZ Ledger uses a CMake-based build system with dedicated build scripts for each platform. The two-script Linux process (`install-deps-linux.sh` + `build-linux.sh`) separates dependency installation (needs root) from the actual build (runs as a regular user).

---

## Requirements

| Component | Version |
|-----------|---------|
| CMake | 3.16+ |
| GCC | 4.8+ |
| Clang | 3.3+ |
| Boost | 1.71+ (with `coroutine` component) |
| OpenSSL | Any recent version |

---

## Linux (Ubuntu 20.04 / 22.04 / 24.04)

### Step 1 — Clone the repository

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
```

### Step 2 — Install dependencies (as root)

```bash
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

Installs: cmake, gcc/g++, git, boost (all components including coroutine/context), openssl, readline, ccache, and compression libraries.

### Step 3 — Build (as regular user)

```bash
chmod +x build-linux.sh
./build-linux.sh
```

Output binary: `build/programs/vizd/vizd`

### Common build flags

```bash
# Low-memory node (validators/seed nodes — excludes history indexing)
./build-linux.sh -l

# Testnet build
./build-linux.sh -n

# Debug build
./build-linux.sh -t Debug

# Parallel jobs
./build-linux.sh -j 8

# Skip dependency installation (already installed)
./build-linux.sh --skip-deps

# Custom Boost / OpenSSL paths
./build-linux.sh --boost-root /opt/boost_1_74_0 --openssl-root /opt/openssl
```

---

## macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

The script installs Xcode Command Line Tools (if needed) and Homebrew dependencies automatically, then configures and builds.

```bash
# With custom Boost path
./build-mac.sh --boost-root /opt/homebrew/opt/boost

# Skip dependency installation
./build-mac.sh --skip-deps
```

---

## Windows (MinGW)

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

Optional environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `VIZ_BUILD_TYPE` | `Release` | `Release` or `Debug` |
| `VIZ_LOW_MEMORY` | `OFF` | `ON` to build low-memory node |
| `VIZ_BUILD_TESTNET` | `OFF` | `ON` for testnet build |
| `VIZ_FULL_STATIC` | `OFF` | `ON` for fully static binary |

---

## Windows (MSVC)

Requires Visual Studio 2019+ with "Desktop development with C++" workload:

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-msvc.bat
```

---

## CMake Options

For direct CMake usage (advanced):

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTNET` | `OFF` | Enable testnet-specific code |
| `LOW_MEMORY_NODE` | `OFF` | Exclude history/indexing plugins |
| `CHAINBASE_CHECK_LOCKING` | `OFF` | Enable lock assertion checks (debug) |
| `BUILD_SHARED_LIBRARIES` | `OFF` | Build shared libraries |
| `USE_PCH` | `OFF` | Enable precompiled headers (faster rebuilds) |

Example:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DLOW_MEMORY_NODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
```

Or use the helper:

```bash
python3 programs/build_helpers/configure_build.py --release
```

---

## Build Targets

| Target | Binary | Description |
|--------|--------|-------------|
| `vizd` | `programs/vizd/vizd` | Main node daemon |
| `cli_wallet` | `programs/cli_wallet/cli_wallet` | Command-line wallet |

---

## Docker Builds

The repository ships four Dockerfiles:

| File | Purpose |
|------|---------|
| `Dockerfile-production` | Full mainnet node (Release) |
| `Dockerfile-testnet` | Testnet node (`BUILD_TESTNET=ON`) |

Build example:

```bash
docker build -f share/vizd/docker/Dockerfile-production -t vizd:local .
```

See [Docker](./docker.md) for full production Docker setup.

---

## Troubleshooting

| Problem | Solution |
|---------|---------|
| `boost/coroutine.hpp` not found | Install `libboost-coroutine-dev` (Ubuntu) or Boost 1.71+ |
| CMake < 3.16 | Install newer CMake from `cmake.org` or Kitware PPA |
| `do not run as root` error | Run `build-linux.sh` as a normal user, not `sudo` |
| Link error on macOS (OpenSSL) | `export OPENSSL_ROOT_DIR=$(brew --prefix openssl)` |
| Out of memory during compilation | Use `-j 2` to reduce parallel jobs |
