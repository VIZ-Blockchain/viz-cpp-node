# Platform Configurations

<cite>
**Referenced Files in This Document**
- [CMakeLists.txt](file://CMakeLists.txt)
- [libraries/CMakeLists.txt](file://libraries/CMakeLists.txt)
- [libraries/chain/CMakeLists.txt](file://libraries/chain/CMakeLists.txt)
- [libraries/network/CMakeLists.txt](file://libraries/network/CMakeLists.txt)
- [libraries/wallet/CMakeLists.txt](file://libraries/wallet/CMakeLists.txt)
- [programs/cli_wallet/CMakeLists.txt](file://programs/cli_wallet/CMakeLists.txt)
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py)
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
This document provides comprehensive platform-specific CMake configuration guidance for the VIZ CPP Node build system. It covers compiler and linker settings per platform, static linking options, required libraries, and troubleshooting steps for common issues. The focus areas include:
- Windows with MSVC and MinGW toolchains
- macOS with libc++ and C++14
- Linux with GCC, pthread, rt, and OpenSSL detection
- Compiler version requirements and platform-specific dependency resolution

## Project Structure
The top-level CMake configuration orchestrates platform-specific behavior and includes subprojects for third-party libraries, core libraries, plugins, and programs. Platform checks are performed early to set flags, link libraries, and enable features.

```mermaid
graph TB
Root["Top-level CMakeLists.txt<br/>Defines project, minimum CMake version,<br/>compiler checks, and platform branches"]
ThirdParty["thirdparty/CMakeLists.txt<br/>Adds appbase, fc, chainbase"]
Libraries["libraries/CMakeLists.txt<br/>Adds api, chain, protocol, network, time, utilities, wallet"]
Plugins["plugins/CMakeLists.txt<br/>Discovers and adds plugins dynamically"]
Programs["programs/CMakeLists.txt<br/>Adds build helpers, cli_wallet, js_operation_serializer, size_checker, util, vizd"]
Root --> ThirdParty
Root --> Libraries
Root --> Plugins
Root --> Programs
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L1-L277)
- [libraries/CMakeLists.txt](file://libraries/CMakeLists.txt#L1-L8)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L1-L277)
- [libraries/CMakeLists.txt](file://libraries/CMakeLists.txt#L1-L8)

## Core Components
- Top-level CMake configuration sets compiler requirements, optional build features, and platform-specific flags.
- Library targets are built with platform-aware compile and link settings.
- Executables link against platform-specific libraries and optional profiling or performance tools.

Key platform-specific behaviors:
- Compiler version checks for GCC and Clang.
- Static linking toggles via a full-static build option.
- Platform-specific include/link flags for Windows, macOS, and Linux.

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L11-L20)
- [CMakeLists.txt](file://CMakeLists.txt#L52-L54)
- [CMakeLists.txt](file://CMakeLists.txt#L158-L202)

## Architecture Overview
The build system applies platform-specific logic early, then composes targets across thirdparty, libraries, plugins, and programs. Windows, macOS, and Linux branches configure flags, libraries, and linkers differently.

```mermaid
graph TB
subgraph "Windows"
WinFlags["MSVC: disable specific warnings<br/>linker flags: /SAFESEH:NO<br/>Debug linker flags: /DEBUG"]
MinGWFlags["MinGW: C++11, -fpermissive, SSE4.2, -Wa,-mbig-obj<br/>Release: -O3<br/>Debug: -O2"]
TclInteg["TCL: locate via TCL_ROOT env var<br/>resolve optimized/debug libs"]
WinStatic["FULL_STATIC_BUILD: -static-libstdc++ -static-libgcc"]
end
subgraph "macOS"
MacFlags["C++14, libc++, -Wall, -Wno-conversion, -Wno-deprecated-declarations"]
MacReadline["Optional readline detection"]
end
subgraph "Linux"
LinFlags["C++14, -Wall"]
LinPthreads["pthread library"]
LinRT["rt library"]
OpenSSLDetect["crypto_library fallback to 'crypto' if not found"]
LinStatic["FULL_STATIC_BUILD: -static-libstdc++ -static-libgcc"]
end
Root["CMakeLists.txt<br/>Compiler checks, options, platform branches"]
Root --> WinFlags
Root --> MinGWFlags
Root --> TclInteg
Root --> WinStatic
Root --> MacFlags
Root --> MacReadline
Root --> LinFlags
Root --> LinPthreads
Root --> LinRT
Root --> OpenSSLDetect
Root --> LinStatic
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L112-L202)

## Detailed Component Analysis

### Windows Configuration (MSVC and MinGW)
- Compiler checks enforce minimum versions for GCC and Clang.
- MSVC-specific flags:
  - Disable specific warnings.
  - Linker flags to bypass Safe Exception Handler (SEH) requirements.
  - Debug linker flags to ensure debug info emission.
- TCL library integration:
  - Locate headers via environment variable pointing to TCL installation.
  - Resolve optimized and debug variants of the TCL library.
- MinGW-specific flags:
  - C++11 standard, permissive mode, SSE4.2, and large object support.
  - Release and Debug optimization levels.
  - Optional static linking of standard C++ and C runtime when enabled.

```mermaid
flowchart TD
Start(["Configure on Windows"]) --> CheckToolchain["Detect MSVC or MinGW"]
CheckToolchain --> |MSVC| MSVCPath["Apply MSVC flags:<br/>disable warnings<br/>/SAFESEH:NO for linker<br/>ensure /DEBUG in Debug"]
CheckToolchain --> |MinGW| MinGWPath["Apply MinGW flags:<br/>-std=c++11 -fpermissive -msse4.2 -Wa,-mbig-obj<br/>-O3 for Release<br/>-O2 for Debug"]
MSVCPath --> TclFind["Locate TCL via TCL_ROOT<br/>resolve optimized/debug libs"]
MinGWPath --> StaticCheck{"FULL_STATIC_BUILD?"}
TclFind --> StaticCheck
StaticCheck --> |Yes| WinStatic["Link with -static-libstdc++ -static-libgcc"]
StaticCheck --> |No| End(["Done"])
WinStatic --> End
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L123-L156)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L112-L156)

### macOS Configuration (Apple Platforms)
- Compiler flags:
  - C++14 standard.
  - Use of libc++ standard library.
  - General warning flags and suppression of specific conversion/deprecation warnings.
- Optional readline detection is present but not mandatory.

```mermaid
flowchart TD
StartMac(["Configure on macOS"]) --> MacFlags["Set C++14 and libc++<br/>apply -Wall and warning suppressions"]
MacFlags --> ReadlineCheck{"readline found?"}
ReadlineCheck --> |Yes| LinkReadline["Link readline"]
ReadlineCheck --> |No| SkipReadline["Proceed without readline"]
LinkReadline --> EndMac(["Done"])
SkipReadline --> EndMac
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L166-L170)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L166-L170)

### Linux Configuration (GNU Toolchain)
- Compiler flags:
  - C++14 standard and general warnings.
- Required libraries:
  - pthread library linkage.
  - rt library linkage.
  - OpenSSL detection fallback to the crypto library if not found by find_package.
- Optional static linking of standard C++ and C runtime when enabled.

```mermaid
flowchart TD
StartLin(["Configure on Linux"]) --> LinFlags["Set C++14 and -Wall"]
LinFlags --> Pthread["Ensure pthread library"]
Pthread --> RTlib["Ensure rt library"]
RTlib --> OpenSSL["If OpenSSL not detected, default to 'crypto'"]
OpenSSL --> StaticCheck{"FULL_STATIC_BUILD?"}
StaticCheck --> |Yes| LinStatic["Link with -static-libstdc++ -static-libgcc"]
StaticCheck --> |No| EndLin(["Done"])
LinStatic --> EndLin
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L171-L184)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L171-L184)

### Static Linking Option
- A dedicated build mode enables full static linking of standard C++ and C runtime libraries on both Windows and Linux platforms.
- The option is surfaced in the build helper script and consumed by the top-level configuration.

```mermaid
sequenceDiagram
participant Dev as "Developer"
participant Helper as "configure_build.py"
participant Root as "CMakeLists.txt"
Dev->>Helper : Invoke build helper
Helper->>Root : Pass -DFULL_STATIC_BUILD=ON
Root->>Root : Apply platform-specific static flags
Root-->>Dev : Configure with static linking
```

**Diagram sources**
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py#L170)
- [CMakeLists.txt](file://CMakeLists.txt#L153-L155)
- [CMakeLists.txt](file://CMakeLists.txt#L181-L183)

**Section sources**
- [programs/build_helpers/configure_build.py](file://programs/build_helpers/configure_build.py#L170)
- [CMakeLists.txt](file://CMakeLists.txt#L153-L155)
- [CMakeLists.txt](file://CMakeLists.txt#L181-L183)

### Library Targets and Platform-Aware Settings
- Library targets are built with platform-specific compile flags and link dependencies.
- Examples:
  - Chain library applies MSVC-specific bigobj handling and links to protocol, utilities, fc, chainbase, appbase, and optional patch merge library.
  - Network library applies MSVC bigobj handling and links to fc and protocol.
  - Wallet library links to numerous internal plugins and platform-specific libraries.

```mermaid
graph TB
Chain["graphene_chain<br/>Compile flags: /bigobj on MSVC<br/>Links: graphene_protocol, graphene_utilities, fc, chainbase, appbase"]
Network["graphene_network<br/>Compile flags: /bigobj on MSVC<br/>Links: fc, graphene_protocol"]
Wallet["graphene_wallet<br/>Links: graphene_network, graphene_chain,<br/>multiple internal plugins, fc, platform libs"]
RootLibs["libraries/CMakeLists.txt<br/>Add subdirectories for api, chain, protocol, network, time, utilities, wallet"]
RootLibs --> Chain
RootLibs --> Network
RootLibs --> Wallet
```

**Diagram sources**
- [libraries/chain/CMakeLists.txt](file://libraries/chain/CMakeLists.txt#L130-L132)
- [libraries/chain/CMakeLists.txt](file://libraries/chain/CMakeLists.txt#L127)
- [libraries/network/CMakeLists.txt](file://libraries/network/CMakeLists.txt#L46-L48)
- [libraries/network/CMakeLists.txt](file://libraries/network/CMakeLists.txt#L39)
- [libraries/wallet/CMakeLists.txt](file://libraries/wallet/CMakeLists.txt#L73-L75)
- [libraries/wallet/CMakeLists.txt](file://libraries/wallet/CMakeLists.txt#L50-L70)

**Section sources**
- [libraries/chain/CMakeLists.txt](file://libraries/chain/CMakeLists.txt#L130-L132)
- [libraries/chain/CMakeLists.txt](file://libraries/chain/CMakeLists.txt#L127)
- [libraries/network/CMakeLists.txt](file://libraries/network/CMakeLists.txt#L46-L48)
- [libraries/network/CMakeLists.txt](file://libraries/network/CMakeLists.txt#L39)
- [libraries/wallet/CMakeLists.txt](file://libraries/wallet/CMakeLists.txt#L73-L75)
- [libraries/wallet/CMakeLists.txt](file://libraries/wallet/CMakeLists.txt#L50-L70)

### Executable Targets and Platform-Specific Libraries
- CLI wallet links to network, chain, protocol, utilities, wallet, multiple internal plugins, fc, and platform-specific libraries.
- On Unix (non-macOS), the rt library is added conditionally.
- On macOS, readline is linked if found.
- Optional performance tooling via gperftools detection.

```mermaid
sequenceDiagram
participant CMake as "CMakeLists.txt"
participant CLI as "cli_wallet target"
participant Libs as "Linked Libraries"
CMake->>CLI : Define target and sources
CMake->>Libs : Add graphene_network, graphene_chain, graphene_protocol, graphene_utilities, graphene_wallet
CMake->>Libs : Add internal plugins and fc
CMake->>Libs : Add ${readline_libraries}, ${CMAKE_DL_LIBS}, ${PLATFORM_SPECIFIC_LIBS}
CMake->>Libs : Add ${Boost_LIBRARIES}
CLI-->>CMake : Target ready for build
```

**Diagram sources**
- [programs/cli_wallet/CMakeLists.txt](file://programs/cli_wallet/CMakeLists.txt#L21-L41)
- [programs/cli_wallet/CMakeLists.txt](file://programs/cli_wallet/CMakeLists.txt#L2-L8)

**Section sources**
- [programs/cli_wallet/CMakeLists.txt](file://programs/cli_wallet/CMakeLists.txt#L21-L41)
- [programs/cli_wallet/CMakeLists.txt](file://programs/cli_wallet/CMakeLists.txt#L2-L8)

## Dependency Analysis
- Compiler and toolchain:
  - Minimum versions enforced for GCC and Clang.
  - Ninja generator receives additional diagnostics flags for Clang.
- Boost:
  - Static usage is enabled by default.
  - Coroutine component is conditionally added for newer Boost versions.
- Platform libraries:
  - Windows: TCL integration via environment-driven discovery.
  - macOS: optional readline linkage.
  - Linux: pthread and rt linkage; OpenSSL fallback to crypto.

```mermaid
graph TB
Compilers["Compiler Checks<br/>GCC >= 4.8<br/>Clang >= 3.3"]
BoostCfg["Boost Static Usage Enabled"]
CoroutineFix["Conditional coroutine component for Boost >= 1.54"]
PlatformLibs["Platform Libraries<br/>Windows: TCL<br/>macOS: readline<br/>Linux: pthread, rt, crypto"]
Root["CMakeLists.txt"]
Root --> Compilers
Root --> BoostCfg
Root --> CoroutineFix
Root --> PlatformLibs
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L12-L20)
- [CMakeLists.txt](file://CMakeLists.txt#L52)
- [CMakeLists.txt](file://CMakeLists.txt#L99-L104)
- [CMakeLists.txt](file://CMakeLists.txt#L160-L180)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L12-L20)
- [CMakeLists.txt](file://CMakeLists.txt#L52)
- [CMakeLists.txt](file://CMakeLists.txt#L99-L104)
- [CMakeLists.txt](file://CMakeLists.txt#L160-L180)

## Performance Considerations
- Optimization flags:
  - MinGW Debug builds use a moderate optimization level to avoid assembler errors.
  - General Debug builds add a debug macro definition.
- Static linking:
  - Enables full static linking of standard C++ and C runtime libraries when requested.
- Build acceleration:
  - Optional ccache usage if available.

[No sources needed since this section provides general guidance]

## Troubleshooting Guide
Common platform-specific issues and resolutions:
- Windows
  - Missing TCL headers or libraries:
    - Ensure TCL_ROOT environment variable points to a valid TCL installation so headers and libraries can be discovered.
  - SEH-related linker warnings or failures:
    - The configuration disables Safe Exception Handler emission; ensure downstream packaging does not require SEH.
  - Large object compilation:
    - MSVC targets apply bigobj handling; verify that sources triggering large object counts still compile.
- macOS
  - Missing readline:
    - readline is optional; if not found, the build proceeds without it. Install readline if interactive editing is desired.
  - Standard library mismatch:
    - Ensure libc++ is used consistently across dependencies.
- Linux
  - pthread or rt missing:
    - The build expects pthread and rt to be available; ensure the system provides these libraries.
  - OpenSSL detection failure:
    - If find_package cannot locate OpenSSL, the configuration defaults to linking against the crypto library; adjust package configuration or pass explicit hints if necessary.
  - Static linking failures:
    - When enabling full static linking, ensure all transitive dependencies are also statically available.

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L132-L145)
- [CMakeLists.txt](file://CMakeLists.txt#L130-L132)
- [CMakeLists.txt](file://CMakeLists.txt#L160-L164)
- [CMakeLists.txt](file://CMakeLists.txt#L174-L180)
- [CMakeLists.txt](file://CMakeLists.txt#L181-L183)

## Conclusion
The VIZ CPP Node build system applies robust, platform-aware configuration to ensure reliable builds across Windows, macOS, and Linux. By enforcing compiler versions, integrating platform-specific libraries, and supporting a static-linking mode, the system accommodates diverse deployment scenarios. Use the platform-specific guidance and troubleshooting tips herein to resolve typical build issues and tailor configurations to your environment.