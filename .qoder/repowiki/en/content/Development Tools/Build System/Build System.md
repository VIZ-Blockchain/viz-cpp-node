# Build System

<cite>
**Referenced Files in This Document**
- [CMakeLists.txt](file://CMakeLists.txt)
- [programs/CMakeLists.txt](file://programs/CMakeLists.txt)
- [libraries/CMakeLists.txt](file://libraries/CMakeLists.txt)
- [plugins/CMakeLists.txt](file://plugins/CMakeLists.txt)
- [thirdparty/CMakeLists.txt](file://thirdparty/CMakeLists.txt)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)
- [programs/util/newplugin.py](file://programs/util/newplugin.py)
- [documentation/building.md](file://documentation/building.md)
- [share/vizd/docker/Dockerfile-production](file://share/vizd/docker/Dockerfile-production)
- [share/vizd/docker/Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem)
- [share/vizd/docker/Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo)
- [share/vizd/docker/Dockerfile-testnet](file://share/vizd/docker/Dockerfile-testnet)
- [share/vizd/vizd.sh](file://share/vizd/vizd.sh)
</cite>

## Update Summary
**Changes Made**
- Updated CMake minimum version requirement from 2.8.12 to 3.16
- Updated Boost library requirement from 1.57 to 1.71
- Removed deprecated Windows-specific Boost 1.53 compatibility logic
- Added coroutine component requirement to Boost components
- Updated Docker build configurations to use Boost 1.71 packages

## Table of Contents
1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Core Components](#core-components)
4. [Architecture Overview](#architecture-overview)
5. [Detailed Component Analysis](#detailed-component-analysis)
6. [Dependency Analysis](#dependency-analysis)
7. [Performance Considerations](#performance-considerations)
8. [Troubleshooting Guide](#troubleshooting-guide)
9. [Conclusion](#conclusion)
10. [Appendices](#appendices)

## Introduction
This document explains the build system for VIZ CPP Node, focusing on the CMake-based configuration, cross-platform compilation support, dependency management, and build targets. It also covers the build helper tools configure_build.py, cat_parts.py, and newplugin.py, and documents Docker-based development and production builds. Practical examples demonstrate common build scenarios such as development, release, and cross-compilation. Finally, it addresses troubleshooting common build issues and how build options influence runtime performance.

## Project Structure
The build system is organized around a top-level CMake project that orchestrates subprojects:
- Top-level CMake project defines compiler checks, options, platform-specific flags, and includes subdirectories for thirdparty, libraries, plugins, and programs.
- Subprojects:
  - libraries: API, chain, protocol, network, time, utilities, wallet
  - plugins: dynamically discovered via a scanning mechanism
  - thirdparty: appbase, fc, chainbase
  - programs: build_helpers, cli_wallet, vizd, js_operation_serializer, size_checker, util

```mermaid
graph TB
Root["Top-level CMakeLists.txt"]
TP["thirdparty/CMakeLists.txt"]
LIB["libraries/CMakeLists.txt"]
PLG["plugins/CMakeLists.txt"]
PRG["programs/CMakeLists.txt"]
Root --> TP
Root --> LIB
Root --> PLG
Root --> PRG
```

**Diagram sources**
- [CMakeLists.txt:206-209](file://CMakeLists.txt#L206-L209)
- [thirdparty/CMakeLists.txt:1-3](file://thirdparty/CMakeLists.txt#L1-L3)
- [libraries/CMakeLists.txt:1-8](file://libraries/CMakeLists.txt#L1-L8)
- [plugins/CMakeLists.txt:1-12](file://plugins/CMakeLists.txt#L1-L12)
- [programs/CMakeLists.txt:1-8](file://programs/CMakeLists.txt#L1-L8)

**Section sources**
- [CMakeLists.txt:1-271](file://CMakeLists.txt#L1-L271)
- [libraries/CMakeLists.txt:1-8](file://libraries/CMakeLists.txt#L1-L8)
- [plugins/CMakeLists.txt:1-12](file://plugins/CMakeLists.txt#L1-L12)
- [thirdparty/CMakeLists.txt:1-3](file://thirdparty/CMakeLists.txt#L1-L3)
- [programs/CMakeLists.txt:1-8](file://programs/CMakeLists.txt#L1-L8)

## Core Components
- Top-level CMake project:
  - Enforces minimum CMake version 3.16 and compiler versions for GCC and Clang.
  - Configures Boost usage with version 1.71 and coroutine component requirement, optional static/shared libraries, and PCH support.
  - Provides compile-time options: BUILD_TESTNET, LOW_MEMORY_NODE, CHAINBASE_CHECK_LOCKING, ENABLE_MONGO_PLUGIN.
  - Sets platform-specific flags for Windows (MSVC/Mingw), macOS, and Linux.
  - Enables ccache globally when available.
  - Supports optional CPack installer generation.
- Subproject inclusion:
  - thirdparty, libraries, plugins, and programs are added as subdirectories.
- Helper scripts:
  - configure_build.py: wraps cmake with sensible defaults and cross-compilation support.
  - cat_parts.py: concatenates files from a directory tree into a single output file.
  - newplugin.py: scaffolds a new plugin directory and files.

**Section sources**
- [CMakeLists.txt:1-271](file://CMakeLists.txt#L1-L271)
- [programs/build_helpers/configure_build.py:1-202](file://programs/build_helpers/configure_build.py#L1-L202)
- [programs/build_helpers/cat_parts.py:1-74](file://programs/build_helpers/cat_parts.py#L1-L74)
- [programs/util/newplugin.py:1-251](file://programs/util/newplugin.py#L1-L251)

## Architecture Overview
The build pipeline integrates CMake configuration, platform detection, dependency discovery, and helper tools to produce binaries and optionally packaging artifacts.

```mermaid
graph TB
subgraph "Host Machine"
CFG["configure_build.py"]
CC["Compiler (GCC/Clang/MSVC/Mingw)"]
CMake["CMake (Top-level)"]
Helpers["cat_parts.py / newplugin.py"]
end
subgraph "Source Tree"
Root["CMakeLists.txt"]
Libs["libraries/*"]
Plugins["plugins/*"]
Third["thirdparty/*"]
Progs["programs/*"]
end
subgraph "Artifacts"
Bin["Binaries (vizd, cli_wallet, ...)"]
Inst["Installed Layout (/usr/local or CPack)"]
end
CFG --> CMake
CMake --> Root
Root --> Libs
Root --> Plugins
Root --> Third
Root --> Progs
CC --> CMake
CMake --> Bin
Helpers --> Root
CMake --> Inst
```

**Diagram sources**
- [CMakeLists.txt:206-209](file://CMakeLists.txt#L206-L209)
- [programs/build_helpers/configure_build.py:143-195](file://programs/build_helpers/configure_build.py#L143-L195)
- [programs/build_helpers/cat_parts.py:11-69](file://programs/build_helpers/cat_parts.py#L11-L69)
- [programs/util/newplugin.py:225-246](file://programs/util/newplugin.py#L225-L246)

## Detailed Component Analysis

### CMake Configuration and Options
Key behaviors:
- CMake minimum version: Requires CMake 3.16 or higher for modern C++ features and improved dependency management.
- Compiler enforcement: Fails early if GCC < 4.8 or Clang < 3.3.
- Boost configuration: Uses Boost 1.71 with coroutine component requirement; supports static usage; removes deprecated 1.53 compatibility logic.
- Platform flags:
  - Windows (MSVC): Adds warning suppressions, disables safe-seh, ensures debug info, locates TCL.
  - Windows (MinGW): Enables C++11, permissive mode, SSE4.2, big obj, sets Release/Debug optimization flags, supports full static build.
  - macOS: Uses libc++, C++14, sets common warnings.
  - Linux: Uses C++14, enables rt and pthread, optional crypto library, supports full static build.
- Coverage/testing: Optional --coverage flag when enabled.
- Options:
  - BUILD_TESTNET: Adds preprocessor defines and prints configuration status.
  - LOW_MEMORY_NODE: Adds preprocessor defines and prints configuration status.
  - CHAINBASE_CHECK_LOCKING: Adds preprocessor defines and prints configuration status.
  - ENABLE_MONGO_PLUGIN: Adds MongoDB plugin linkage and preprocessor defines.
  - BUILD_SHARED_LIBRARIES: Defaults to off.
  - USE_PCH: Optional cotire precompiled headers support.
  - ENABLE_INSTALLER: Optional CPack packaging.

Build targets:
- The top-level project includes subprojects for thirdparty, libraries, plugins, and programs. Programs include build_helpers, cli_wallet, vizd, js_operation_serializer, size_checker, and util.

**Section sources**
- [CMakeLists.txt:2-3](file://CMakeLists.txt#L2-L3)
- [CMakeLists.txt:11-20](file://CMakeLists.txt#L11-L20)
- [CMakeLists.txt:38-49](file://CMakeLists.txt#L38-L49)
- [CMakeLists.txt:51-53](file://CMakeLists.txt#L51-L53)
- [CMakeLists.txt:55-80](file://CMakeLists.txt#L55-L80)
- [CMakeLists.txt:82-88](file://CMakeLists.txt#L82-L88)
- [CMakeLists.txt:96-100](file://CMakeLists.txt#L96-L100)
- [CMakeLists.txt:108-152](file://CMakeLists.txt#L108-L152)
- [CMakeLists.txt:154-198](file://CMakeLists.txt#L154-L198)
- [programs/CMakeLists.txt:1-8](file://programs/CMakeLists.txt#L1-L8)
- [libraries/CMakeLists.txt:1-8](file://libraries/CMakeLists.txt#L1-L8)
- [plugins/CMakeLists.txt:1-12](file://plugins/CMakeLists.txt#L1-L12)
- [thirdparty/CMakeLists.txt:1-3](file://thirdparty/CMakeLists.txt#L1-L3)

### Cross-Platform Compilation Flags and Toolchains
- Windows:
  - MSVC: Warning suppressions, safe-seh disable, ensures debug info, finds TCL.
  - MinGW: C++11, permissive mode, SSE4.2, big obj, Release/Debug optimization, optional full static build.
- macOS:
  - C++14, libc++, common warnings, disables certain conversion warnings.
- Linux:
  - C++14, rt and pthread libraries, optional crypto library, optional full static build.
- Ninja + Clang diagnostics: colorized diagnostics when generator is Ninja and compiler is Clang.
- Debug build: defines DEBUG automatically.

**Section sources**
- [CMakeLists.txt:108-152](file://CMakeLists.txt#L108-L152)
- [CMakeLists.txt:154-198](file://CMakeLists.txt#L154-L198)

### Dependency Management
- Boost: Required version 1.71 with components including thread, date_time, system, filesystem, program_options, serialization, chrono, unit_test_framework, context, locale, and coroutine. Static usage is preferred; coroutine is now a mandatory component for Boost >= 1.71.
- OpenSSL: Optional via OPENSSL_ROOT_DIR; used when present.
- Readline: Found on non-Windows platforms; included if available.
- Crypto library: Defaults to crypto on Linux; configurable.
- ccache: Detected and used globally for compile/link steps when available.

**Updated** Enhanced Boost dependency requirements with version 1.71 and mandatory coroutine component

**Section sources**
- [CMakeLists.txt:38-49](file://CMakeLists.txt#L38-L49)
- [CMakeLists.txt:96-100](file://CMakeLists.txt#L96-L100)
- [CMakeLists.txt:102-106](file://CMakeLists.txt#L102-L106)
- [CMakeLists.txt:156-160](file://CMakeLists.txt#L156-L160)
- [CMakeLists.txt:172-176](file://CMakeLists.txt#L172-L176)

### Build Targets
- Programs:
  - vizd: main node binary.
  - cli_wallet: command-line wallet.
  - js_operation_serializer: utility for JS operation serialization.
  - size_checker: utility for size analysis.
  - build_helpers: helper utilities.
  - util: various utilities.
- Libraries:
  - api, chain, protocol, network, time, utilities, wallet.
- Plugins:
  - Discovered dynamically via scanning for subdirectories with CMakeLists.txt.

**Section sources**
- [programs/CMakeLists.txt:1-8](file://programs/CMakeLists.txt#L1-L8)
- [libraries/CMakeLists.txt:1-8](file://libraries/CMakeLists.txt#L1-L8)
- [plugins/CMakeLists.txt:1-12](file://plugins/CMakeLists.txt#L1-L12)

### Build Helper Tools

#### configure_build.py
- Purpose: Simplifies invoking cmake with sensible defaults and cross-compilation support.
- Features:
  - Accepts --sys-root, --boost-dir, --openssl-dir to guide find modules.
  - Supports LOW_MEMORY_NODE and CMAKE_BUILD_TYPE toggles.
  - Supports Windows cross-compilation via MinGW with static linking flags and root path modes.
  - Passes additional cmake options after a separator.
- Typical usage:
  - Configure for Release with LOW_MEMORY_NODE=OFF.
  - Configure for Debug with LOW_MEMORY_NODE=ON.
  - Cross-compile for Windows using MinGW with static linking.

```mermaid
sequenceDiagram
participant Dev as "Developer"
participant Script as "configure_build.py"
participant CMake as "CMake"
participant FS as "Filesystem"
Dev->>Script : Invoke with options (--boost-dir, --openssl-dir, --sys-root, -w/--windows, -f/-w, -r/-d, -- [CMAKEOPTS])
Script->>Script : Parse arguments and validate paths
Script->>Script : Detect Boost version and construct flags
Script->>Script : Optionally enable Windows cross-compilation flags
Script->>Script : Set LOW_MEMORY_NODE and CMAKE_BUILD_TYPE
Script->>CMake : Run cmake with constructed arguments
CMake->>FS : Discover Boost/OpenSSL/dependencies
CMake-->>Dev : Configure result and build instructions
```

**Diagram sources**
- [programs/build_helpers/configure_build.py:35-119](file://programs/build_helpers/configure_build.py#L35-L119)
- [programs/build_helpers/configure_build.py:143-195](file://programs/build_helpers/configure_build.py#L143-L195)

**Section sources**
- [programs/build_helpers/configure_build.py:1-202](file://programs/build_helpers/configure_build.py#L1-L202)

#### cat_parts.py
- Purpose: Concatenates files from a directory tree into a single output file, preserving order and skipping non-files.
- Behavior:
  - Validates input directory and output file path.
  - Skips non-file entries and filters by suffix if requested.
  - Compares generated content with existing output to avoid unnecessary writes.
  - Creates parent directories if missing.

```mermaid
flowchart TD
Start(["Start"]) --> Args["Parse input_dir and out_file"]
Args --> Validate{"input_dir is dir<br/>out_file path valid?"}
Validate --> |No| Error["Print usage and exit"]
Validate --> |Yes| Scan["Scan files in input_dir"]
Scan --> Filter["Filter by suffix (optional)"]
Filter --> Sort["Sort files lexicographically"]
Sort --> Read["Read file contents in order"]
Read --> Compare{"Output exists and matches?"}
Compare --> |Yes| UpToDate["Print 'up-to-date' and exit"]
Compare --> |No| Write["Write concatenated content to out_file"]
Write --> Done(["Done"])
Error --> Done
UpToDate --> Done
```

**Diagram sources**
- [programs/build_helpers/cat_parts.py:11-69](file://programs/build_helpers/cat_parts.py#L11-L69)

**Section sources**
- [programs/build_helpers/cat_parts.py:1-74](file://programs/build_helpers/cat_parts.py#L1-L74)

#### newplugin.py
- Purpose: Generates boilerplate files for a new plugin under libraries/plugins/<plugin_name>.
- Templates:
  - CMakeLists.txt for the plugin target.
  - Plugin header and implementation.
  - API header and implementation.
- Behavior:
  - Accepts provider and plugin name.
  - Writes files into a dedicated directory under libraries/plugins/<plugin_name>.

```mermaid
flowchart TD
Start(["Start"]) --> Parse["Parse provider and name"]
Parse --> MakeDir["Ensure output directory exists"]
MakeDir --> WriteFiles["Write template files:<br/>CMakeLists.txt,<br/>plugin header/impl,<br/>api header/impl"]
WriteFiles --> Done(["Done"])
```

**Diagram sources**
- [programs/util/newplugin.py:225-246](file://programs/util/newplugin.py#L225-L246)

**Section sources**
- [programs/util/newplugin.py:1-251](file://programs/util/newplugin.py#L1-L251)

### Docker-Based Builds
The repository ships Dockerfiles for multiple environments:
- Production: Full node build with Release, shared libraries disabled, lock checking disabled, MongoDB plugin disabled.
- Low-memory: Same as production but with LOW_MEMORY_NODE enabled.
- Mongo: Installs MongoDB C/C++ drivers and enables ENABLE_MONGO_PLUGIN.
- Testnet: Builds with BUILD_TESTNET enabled.

Each Dockerfile:
- Uses a two-stage build to minimize image size.
- Copies only necessary source files to reduce rebuilds.
- Runs cmake with explicit options and compiles with parallel jobs.
- Installs artifacts and prepares runtime configuration files and volumes.

**Updated** Docker configurations now use Boost 1.71 packages (libboost-coroutine-dev, libboost-context-dev) instead of older versions

```mermaid
graph TB
subgraph "Builder Stage"
Dkfile["Dockerfile-*"]
Deps["Install build deps"]
CopySrc["Copy minimal sources"]
GitSub["Init submodules"]
CMakeCfg["Run cmake with options"]
Make["Compile with make -j$(nproc)"]
Install["make install"]
end
subgraph "Runtime Stage"
Runtime["Base image"]
User["Create vizd user"]
Vars["Set volumes and ports"]
Cfg["Copy configs and snapshots"]
end
Dkfile --> Deps --> CopySrc --> GitSub --> CMakeCfg --> Make --> Install --> Runtime
Runtime --> User --> Vars --> Cfg
```

**Diagram sources**
- [share/vizd/docker/Dockerfile-production:1-98](file://share/vizd/docker/Dockerfile-production#L1-L98)
- [share/vizd/docker/Dockerfile-lowmem:1-80](file://share/vizd/docker/Dockerfile-lowmem#L1-L80)
- [share/vizd/docker/Dockerfile-mongo:1-109](file://share/vizd/docker/Dockerfile-mongo#L1-L109)
- [share/vizd/docker/Dockerfile-testnet:1-98](file://share/vizd/docker/Dockerfile-testnet#L1-L98)

**Section sources**
- [share/vizd/docker/Dockerfile-production:56-62](file://share/vizd/docker/Dockerfile-production#L56-L62)
- [share/vizd/docker/Dockerfile-lowmem:43-49](file://share/vizd/docker/Dockerfile-lowmem#L43-L49)
- [share/vizd/docker/Dockerfile-mongo:72-78](file://share/vizd/docker/Dockerfile-mongo#L72-L78)
- [share/vizd/docker/Dockerfile-testnet:56-62](file://share/vizd/docker/Dockerfile-testnet#L56-L62)

## Dependency Analysis
- Coupling:
  - Top-level CMake depends on subproject CMakeLists.txt files to register targets.
  - configure_build.py depends on filesystem layout and optional environment variables for Boost/OpenSSL.
  - cat_parts.py depends on directory structure and file suffix filtering.
  - newplugin.py depends on the libraries/plugins directory layout.
- External dependencies:
  - Boost 1.71 (required), OpenSSL (optional), Readline (optional), ccache (optional), MongoDB drivers (optional).
- Indirect dependencies:
  - Plugins are discovered dynamically; their presence affects the build graph.

```mermaid
graph LR
CMakeTop["CMakeLists.txt"]
Libs["libraries/CMakeLists.txt"]
Plugins["plugins/CMakeLists.txt"]
Third["thirdparty/CMakeLists.txt"]
Progs["programs/CMakeLists.txt"]
CFGPy["configure_build.py"]
CATPy["cat_parts.py"]
NEWPy["newplugin.py"]
CMakeTop --> Libs
CMakeTop --> Plugins
CMakeTop --> Third
CMakeTop --> Progs
CFGPy --> CMakeTop
CATPy --> CMakeTop
NEWPy --> Libs
```

**Diagram sources**
- [CMakeLists.txt:206-209](file://CMakeLists.txt#L206-L209)
- [libraries/CMakeLists.txt:1-8](file://libraries/CMakeLists.txt#L1-L8)
- [plugins/CMakeLists.txt:1-12](file://plugins/CMakeLists.txt#L1-L12)
- [thirdparty/CMakeLists.txt:1-3](file://thirdparty/CMakeLists.txt#L1-L3)
- [programs/CMakeLists.txt:1-8](file://programs/CMakeLists.txt#L1-L8)
- [programs/build_helpers/configure_build.py:143-195](file://programs/build_helpers/configure_build.py#L143-L195)
- [programs/build_helpers/cat_parts.py:11-69](file://programs/build_helpers/cat_parts.py#L11-L69)
- [programs/util/newplugin.py:225-246](file://programs/util/newplugin.py#L225-L246)

**Section sources**
- [CMakeLists.txt:206-209](file://CMakeLists.txt#L206-L209)
- [programs/build_helpers/configure_build.py:143-195](file://programs/build_helpers/configure_build.py#L143-L195)
- [programs/build_helpers/cat_parts.py:11-69](file://programs/build_helpers/cat_parts.py#L11-L69)
- [programs/util/newplugin.py:225-246](file://programs/util/newplugin.py#L225-L246)

## Performance Considerations
- Compiler flags:
  - Release builds use aggressive optimization for MinGW and Linux.
  - Debug builds define DEBUG and can be paired with coverage instrumentation when enabled.
- Precompiled headers:
  - USE_PCH enables cotire for faster incremental builds.
- Static vs shared libraries:
  - BUILD_SHARED_LIBRARIES defaults to off, reducing runtime dependencies and potentially improving startup performance.
- Memory profile:
  - LOW_MEMORY_NODE reduces storage overhead by excluding non-consensus data, beneficial for resource-constrained nodes.
- Lock checking:
  - CHAINBASE_CHECK_LOCKING can be disabled for production builds to reduce overhead.
- ccache:
  - Global launch wrappers accelerate rebuilds when available.

Practical implications:
- Choose Release for production builds to maximize runtime performance.
- Disable CHAINBASE_CHECK_LOCKING and LOW_MEMORY_NODE unless required for specific roles.
- Enable USE_PCH for faster local development cycles.

**Section sources**
- [CMakeLists.txt:144-152](file://CMakeLists.txt#L144-L152)
- [CMakeLists.txt:176-183](file://CMakeLists.txt#L176-L183)
- [CMakeLists.txt:51-53](file://CMakeLists.txt#L51-L53)
- [CMakeLists.txt:65-73](file://CMakeLists.txt#L65-L73)
- [CMakeLists.txt:75-80](file://CMakeLists.txt#L75-L80)
- [CMakeLists.txt:102-106](file://CMakeLists.txt#L102-L106)

## Troubleshooting Guide
Common issues and resolutions:
- Boost version mismatch:
  - Ensure Boost version 1.71 or higher satisfies requirements; configure_build.py reads boost/version.hpp to infer version.
  - **Updated** Minimum Boost version increased from 1.57 to 1.71.
- Missing OpenSSL:
  - Set OPENSSL_ROOT_DIR to point to the installation prefix.
- Windows toolchain:
  - MSVC: Verify debug info flags and safe-seh settings; ensure TCL is found if needed.
  - MinGW: Confirm compiler supports C++11 and SSE4.2; use FULL_STATIC_BUILD if distributing static binaries.
- macOS:
  - Use libc++ and appropriate SDK; ensure Boost and OpenSSL are installed via package managers.
- Linux:
  - Ensure rt, pthread, and crypto libraries are available; static builds require static variants of libstdc++/libgcc.
  - **Updated** Docker builds now use libboost-coroutine-dev and libboost-context-dev packages.
- ccache:
  - If builds fail with unexpected cache behavior, temporarily disable ccache or clear the cache.
- Plugin discovery:
  - Ensure plugin subdirectories contain a CMakeLists.txt; plugins are discovered automatically.
- Docker builds:
  - For mongo builds, confirm MongoDB C/C++ drivers are installed prior to cmake invocation.
  - For testnet builds, ensure BUILD_TESTNET is set during configuration.

**Updated** Enhanced Boost dependency requirements and Docker configuration updates

**Section sources**
- [programs/build_helpers/configure_build.py:122-140](file://programs/build_helpers/configure_build.py#L122-L140)
- [CMakeLists.txt:96-100](file://CMakeLists.txt#L96-L100)
- [CMakeLists.txt:108-152](file://CMakeLists.txt#L108-L152)
- [CMakeLists.txt:156-160](file://CMakeLists.txt#L156-L160)
- [CMakeLists.txt:172-176](file://CMakeLists.txt#L172-L176)
- [share/vizd/docker/Dockerfile-production:20-22](file://share/vizd/docker/Dockerfile-production#L20-L22)
- [share/vizd/docker/Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem#L19)
- [share/vizd/docker/Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L19)

## Conclusion
The VIZ CPP Node build system centers on a robust CMake configuration with strong cross-platform support, clear options for memory and performance tuning, and practical helper tools. Recent updates include upgrading to CMake 3.16, Boost 1.71, and adding coroutine component requirements. Dockerfiles streamline both development and production workflows with updated dependency specifications. By leveraging configure_build.py, developers can quickly set up consistent builds across platforms, while cat_parts.py and newplugin.py automate common tasks. Proper selection of build options and compiler flags yields predictable runtime performance and maintainable deployment artifacts.

## Appendices

### Practical Build Scenarios

- Development build (Linux/macOS):
  - Configure with Release and LOW_MEMORY_NODE=OFF.
  - Use ccache for faster rebuilds.
  - Example invocation pattern: cmake -DCMAKE_BUILD_TYPE=Release -DLOW_MEMORY_NODE=OFF ..

- Release build (Linux):
  - Use Release with shared libraries disabled and lock checking disabled for production.
  - Example invocation pattern: cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBRARIES=FALSE -DCHAINBASE_CHECK_LOCKING=FALSE ..

- Cross-compilation (Windows using MinGW):
  - Use configure_build.py with --windows to enable static linking and proper root path modes.
  - Example invocation pattern: python3 configure_build.py --windows --src ../..

- Low-memory node:
  - Enable LOW_MEMORY_NODE for consensus-only nodes.
  - Example invocation pattern: cmake -DLOW_MEMORY_NODE=ON ..

- Testnet build:
  - Enable BUILD_TESTNET for testnet configuration.
  - Example invocation pattern: cmake -DBUILD_TESTNET=TRUE ..

- MongoDB-enabled build:
  - Install MongoDB C/C++ drivers and enable ENABLE_MONGO_PLUGIN.
  - Example invocation pattern: cmake -DENABLE_MONGO_PLUGIN=TRUE ..

- Docker builds:
  - Production: docker build -f share/vizd/docker/Dockerfile-production -t viz-world .
  - Low-memory: docker build -f share/vizd/docker/Dockerfile-lowmem -t viz-world-lowmem .
  - Mongo: docker build -f share/vizd/docker/Dockerfile-mongo -t viz-world-mongo .
  - Testnet: docker build -f share/vizd/docker/Dockerfile-testnet -t viz-world-testnet .

**Updated** Enhanced Docker configurations with Boost 1.71 dependencies

**Section sources**
- [documentation/building.md:1-323](file://documentation/building.md#L1-L323)
- [programs/build_helpers/configure_build.py:168-184](file://programs/build_helpers/configure_build.py#L168-L184)
- [share/vizd/docker/Dockerfile-production:56-62](file://share/vizd/docker/Dockerfile-production#L56-L62)
- [share/vizd/docker/Dockerfile-lowmem:43-49](file://share/vizd/docker/Dockerfile-lowmem#L43-L49)
- [share/vizd/docker/Dockerfile-mongo:72-78](file://share/vizd/docker/Dockerfile-mongo#L72-L78)
- [share/vizd/docker/Dockerfile-testnet:56-62](file://share/vizd/docker/Dockerfile-testnet#L56-L62)