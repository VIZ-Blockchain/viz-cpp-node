# Build Helper Scripts

<cite>
**Referenced Files in This Document**
- [CMakeLists.txt](file://CMakeLists.txt)
- [build-linux.sh](file://build-linux.sh)
- [build-mac.sh](file://build-mac.sh)
- [build-mingw.bat](file://build-mingw.bat)
- [build-msvc.bat](file://build-msvc.bat)
- [programs/build_helpers/CMakeLists.txt](file://programs/build_helpers/CMakeLists.txt)
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)
- [libraries/chain/hardfork.d/0-preamble.hf](file://libraries/chain/hardfork.d/0-preamble.hf)
- [libraries/chain/hardfork.d/1.hf](file://libraries/chain/hardfork.d/1.hf)
- [documentation/building.md](file://documentation/building.md)
</cite>

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

## Introduction
This document explains the build helper scripts and automation tools used to compile the VIZ blockchain node across multiple platforms. It covers the shell and batch scripts for Linux/macOS/Windows, the CMake-based build system, and specialized helper utilities that assist with hardfork file concatenation, reflection validation, and cross-platform configuration.

## Project Structure
The build system is organized around:
- Platform-specific build scripts for quick configuration and compilation
- A centralized CMake configuration that defines compile-time options and platform flags
- Helper utilities under programs/build_helpers for specialized tasks like hardfork file assembly and reflection consistency checks
- Documentation that describes all supported build options and workflows

```mermaid
graph TB
subgraph "Platform Build Scripts"
LNX["build-linux.sh"]
MAC["build-mac.sh"]
MGW["build-mingw.bat"]
MSC["build-msvc.bat"]
end
subgraph "CMake Configuration"
CML["CMakeLists.txt"]
HF0["libraries/chain/hardfork.d/0-preamble.hf"]
HF1["libraries/chain/hardfork.d/1.hf"]
end
subgraph "Build Helpers"
CATCPP["programs/build_helpers/cat-parts.cpp"]
CATPY["programs/build_helpers/cat_parts.py"]
REFCHK["programs/build_helpers/check_reflect.py"]
CFGPY["programs/build_helpers/configure_build.py"]
end
LNX --> CML
MAC --> CML
MGW --> CML
MSC --> CML
CML --> HF0
CML --> HF1
CML --> CATCPP
CML --> CATPY
CML --> REFCHK
CML --> CFGPY
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt)
- [build-linux.sh](file://build-linux.sh)
- [build-mac.sh](file://build-mac.sh)
- [build-mingw.bat](file://build-mingw.bat)
- [build-msvc.bat](file://build-msvc.bat)
- [programs/build_helpers/CMakeLists.txt](file://programs/build_helpers/CMakeLists.txt)
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)
- [libraries/chain/hardfork.d/0-preamble.hf](file://libraries/chain/hardfork.d/0-preamble.hf)
- [libraries/chain/hardfork.d/1.hf](file://libraries/chain/hardfork.d/1.hf)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt)
- [documentation/building.md](file://documentation/building.md)

## Core Components
- Platform build scripts:
  - Linux: automated dependency installation, submodule initialization, CMake configuration, parallel build, and optional install
  - macOS: Xcode/Homebrew checks, OpenSSL detection, CMake configuration, parallel build, and optional install
  - Windows MSVC: CMake configuration with Visual Studio generator, build execution, and optional install
  - Windows MinGW: CMake configuration with MinGW generator, build execution, and optional install
- CMake configuration:
  - Centralized compile-time options (build type, memory mode, testnet, MongoDB plugin, shared/static libs)
  - Platform-specific compiler flags and static linking policies
  - Hardfork file inclusion and generation pipeline
- Build helper utilities:
  - Concatenate hardfork .hf files into a single header
  - Validate FC_REFLECT declarations match Doxygen-extracted class members
  - Cross-platform CMake configuration helper for Windows builds
  - Python-based file concatenation utility

**Section sources**
- [build-linux.sh](file://build-linux.sh)
- [build-mac.sh](file://build-mac.sh)
- [build-mingw.bat](file://build-mingw.bat)
- [build-msvc.bat](file://build-msvc.bat)
- [CMakeLists.txt](file://CMakeLists.txt)
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)

## Architecture Overview
The build architecture integrates platform scripts with CMake and helper utilities to provide a unified, repeatable build experience across platforms.

```mermaid
graph TB
subgraph "User Workflow"
U1["Select platform script"]
U2["Configure options"]
U3["Run build"]
end
subgraph "Build System"
S1["Platform script"]
S2["CMake configuration"]
S3["Compiler and linker"]
S4["Helper utilities"]
end
subgraph "Outputs"
O1["Executable binaries"]
O2["Documentation artifacts"]
O3["Hardfork headers"]
end
U1 --> S1
U2 --> S2
U3 --> S2
S4 --> S2
S2 --> O1
S2 --> O3
S4 --> O2
```

**Diagram sources**
- [build-linux.sh](file://build-linux.sh)
- [build-mac.sh](file://build-mac.sh)
- [build-mingw.bat](file://build-mingw.bat)
- [build-msvc.bat](file://build-msvc.bat)
- [CMakeLists.txt](file://CMakeLists.txt)
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)

## Detailed Component Analysis

### Linux Build Script
The Linux script automates dependency installation (based on detected package manager), submodule initialization, CMake configuration with configurable options, parallel build execution, and optional installation.

```mermaid
flowchart TD
Start(["Script start"]) --> ParseArgs["Parse CLI arguments"]
ParseArgs --> DetectJobs["Detect parallel jobs"]
DetectJobs --> CheckDir["Verify source directory"]
CheckDir --> InstallDeps{"Skip deps?"}
InstallDeps --> |Yes| InitSubmodules["Initialize submodules"]
InstallDeps --> |No| InstallDepsFn["Install dependencies"]
InstallDepsFn --> InitSubmodules
InitSubmodules --> Configure["CMake configure with options"]
Configure --> Build["Parallel build"]
Build --> InstallOpt{"Install?"}
InstallOpt --> |Yes| DoInstall["Install to system"]
InstallOpt --> |No| SkipInstall["Skip install"]
DoInstall --> Done(["Complete"])
SkipInstall --> Done
```

**Diagram sources**
- [build-linux.sh](file://build-linux.sh)

**Section sources**
- [build-linux.sh](file://build-linux.sh)
- [documentation/building.md](file://documentation/building.md)

### macOS Build Script
The macOS script verifies Xcode Command Line Tools and Homebrew, detects OpenSSL path, initializes submodules, configures CMake, builds in parallel, and optionally installs.

```mermaid
flowchart TD
Start(["Script start"]) --> CheckXcode["Check Xcode Command Line Tools"]
CheckXcode --> CheckBrew["Check Homebrew"]
CheckBrew --> InstallDeps{"Skip deps?"}
InstallDeps --> |Yes| DetectOpenSSL["Detect OpenSSL path"]
InstallDeps --> |No| InstallDepsFn["Install dependencies via Homebrew"]
InstallDepsFn --> DetectOpenSSL
DetectOpenSSL --> InitSubmodules["Initialize submodules"]
InitSubmodules --> Configure["CMake configure with options"]
Configure --> Build["Parallel build"]
Build --> InstallOpt{"Install?"}
InstallOpt --> |Yes| DoInstall["Install to system"]
InstallOpt --> |No| SkipInstall["Skip install"]
DoInstall --> Done(["Complete"])
SkipInstall --> Done
```

**Diagram sources**
- [build-mac.sh](file://build-mac.sh)

**Section sources**
- [build-mac.sh](file://build-mac.sh)
- [documentation/building.md](file://documentation/building.md)

### Windows Build Scripts
Two Windows scripts support different toolchains:
- MSVC: Uses Visual Studio generator and CMake configuration with optional extra flags
- MinGW: Uses MinGW Makefiles generator and CMake configuration with static/full-static options

```mermaid
sequenceDiagram
participant User as "User"
participant Script as "Windows Build Script"
participant CMake as "CMake"
participant Generator as "Generator (MSVC/MinGW)"
participant Build as "Build System"
User->>Script : Invoke script with environment variables
Script->>CMake : Configure with generators and options
CMake->>Generator : Select generator and toolchain
Generator-->>CMake : Toolchain configured
CMake-->>Script : Configuration complete
Script->>Build : Build with selected generator
Build-->>Script : Build complete
Script-->>User : Report completion
```

**Diagram sources**
- [build-msvc.bat](file://build-msvc.bat)
- [build-mingw.bat](file://build-mingw.bat)

**Section sources**
- [build-msvc.bat](file://build-msvc.bat)
- [build-mingw.bat](file://build-mingw.bat)
- [documentation/building.md](file://documentation/building.md)

### CMake Configuration and Compile-Time Options
CMake centralizes build configuration, compile-time flags, and platform-specific settings. Key options include build type, memory mode, testnet, MongoDB plugin, shared/static libraries, and chainbase lock checking.

```mermaid
flowchart TD
Start(["CMake configure"]) --> ReadOptions["Read compile-time options"]
ReadOptions --> PlatformFlags["Apply platform-specific flags"]
PlatformFlags --> FindDeps["Find Boost/OpenSSL"]
FindDeps --> GenerateTargets["Generate build targets"]
GenerateTargets --> WriteFiles["Write compile_commands.json"]
WriteFiles --> End(["Ready to build"])
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt)
- [documentation/building.md](file://documentation/building.md)

### Hardfork File Concatenation Utility (C++)
The C++ utility scans a directory for .hf files, sorts them, concatenates their contents, and writes a single output file if the content differs from existing output.

```mermaid
flowchart TD
Start(["Start"]) --> Args["Validate arguments"]
Args --> Scan["Scan directory for .hf files"]
Scan --> Sort["Sort files lexicographically"]
Sort --> Read["Read concatenated content"]
Read --> Compare{"Compare with existing output?"}
Compare --> |Same| UpToDate["Mark as up-to-date"]
Compare --> |Different| Write["Write new output"]
UpToDate --> End(["Exit"])
Write --> End
```

**Diagram sources**
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)

**Section sources**
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)
- [libraries/chain/hardfork.d/0-preamble.hf](file://libraries/chain/hardfork.d/0-preamble.hf)
- [libraries/chain/hardfork.d/1.hf](file://libraries/chain/hardfork.d/1.hf)

### Hardfork File Concatenation Utility (Python)
The Python utility provides equivalent functionality to the C++ version with robust error handling and directory creation.

```mermaid
flowchart TD
Start(["Start"]) --> Args["Validate arguments"]
Args --> Exists{"Output exists?"}
Exists --> |Yes| ReadOld["Read existing output"]
ReadOld --> Compute["Compute new concatenated content"]
Compute --> Same{"Content same?"}
Same --> |Yes| Exit0["Exit 0 (up-to-date)"]
Same --> |No| Write["Write new content"]
Exists --> |No| MkDir["Ensure parent directory exists"]
MkDir --> Compute
Write --> Exit0
```

**Diagram sources**
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)

**Section sources**
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)

### Reflection Consistency Checker
The Python script validates that FC_REFLECT declarations match Doxygen-extracted class members, reporting mismatches and duplicates.

```mermaid
flowchart TD
Start(["Start"]) --> ParseDoxygen["Parse Doxygen XML index"]
ParseDoxygen --> ExtractMembers["Extract class members"]
ExtractMembers --> WalkSources["Walk source tree for FC_REFLECT"]
WalkSources --> Compare["Compare member sets"]
Compare --> Report["Report OK/Not Evaluated/Error items"]
Report --> Exit(["Exit with status"])
```

**Diagram sources**
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)

**Section sources**
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)

### Cross-Platform CMake Configuration Helper (Windows)
The Python helper constructs CMake commands with platform-specific flags, environment variable support, and optional additional options.

```mermaid
sequenceDiagram
participant Dev as "Developer"
participant Helper as "configure_build.py"
participant CMake as "CMake"
participant FS as "Filesystem"
Dev->>Helper : Provide options (paths, flags)
Helper->>FS : Resolve paths and validate
Helper->>Helper : Detect Boost version
Helper->>CMake : Build command with flags
CMake-->>Helper : Execute configuration
Helper-->>Dev : Print command and exit code
```

**Diagram sources**
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)

**Section sources**
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)

## Dependency Analysis
The build system exhibits clear separation of concerns:
- Platform scripts depend on CMake and system tools
- CMake depends on Boost and OpenSSL availability
- Helper utilities are standalone and can be invoked independently
- Hardfork concatenation utilities depend on filesystem access and input ordering

```mermaid
graph TB
LNX["build-linux.sh"] --> CML["CMakeLists.txt"]
MAC["build-mac.sh"] --> CML
MGW["build-mingw.bat"] --> CML
MSC["build-msvc.bat"] --> CML
CATCPP["cat-parts.cpp"] --> HFDIR["hardfork.d/*.hf"]
CATPY["cat_parts.py"] --> HFDIR
REFCHK["check_reflect.py"] --> SRC["Source tree"]
CFGPY["configure_build.py"] --> CML
CML --> BOOST["Boost libraries"]
CML --> SSL["OpenSSL libraries"]
```

**Diagram sources**
- [build-linux.sh](file://build-linux.sh)
- [build-mac.sh](file://build-mac.sh)
- [build-mingw.bat](file://build-mingw.bat)
- [build-msvc.bat](file://build-msvc.bat)
- [CMakeLists.txt](file://CMakeLists.txt)
- [programs/build_helpers/cat-parts.cpp](file://programs/build_helpers/cat-parts.cpp)
- [programs/build_helpers/cat_parts.py](file://programs/build_helpers/cat_parts.py)
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)
- [libraries/chain/hardfork.d/0-preamble.hf](file://libraries/chain/hardfork.d/0-preamble.hf)
- [libraries/chain/hardfork.d/1.hf](file://libraries/chain/hardfork.d/1.hf)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt)
- [programs/build_helpers/CMakeLists.txt](file://programs/build_helpers/CMakeLists.txt)

## Performance Considerations
- Parallel builds: All platform scripts support parallel job counts to speed up compilation
- Dependency caching: CMake and ccache integration reduce rebuild times
- Static linking: Windows scripts offer full static builds to simplify deployment
- Hardfork concatenation: Efficient sorting and streaming minimize I/O overhead

## Troubleshooting Guide
- Linux/macOS dependency issues: Use the provided scripts to install required packages; verify submodules are initialized
- Windows environment variables: Ensure BOOST_ROOT and OPENSSL_ROOT_DIR are set and point to valid directories
- Hardfork header mismatch: Re-run the concatenation utility to regenerate the combined header
- Reflection validation failures: Fix FC_REFLECT declarations to match Doxygen-extracted members
- CMake configuration errors: Confirm compiler versions meet minimum requirements and required libraries are discoverable

**Section sources**
- [documentation/building.md](file://documentation/building.md)
- [build-linux.sh](file://build-linux.sh)
- [build-mac.sh](file://build-mac.sh)
- [build-mingw.bat](file://build-mingw.bat)
- [build-msvc.bat](file://build-msvc.bat)
- [programs/build_helpers/check_reflect.py](file://programs/build_helpers/check_reflect.py)

## Conclusion
The build helper scripts and CMake configuration provide a robust, cross-platform build system for the VIZ node. They automate dependency management, platform-specific configurations, and quality checks, enabling contributors and operators to build reliably across Linux, macOS, and Windows environments.