# Docker Integration

<cite>
**Referenced Files in This Document**
- [Dockerfile-production](file://share/vizd/docker/Dockerfile-production)
- [Dockerfile-testnet](file://share/vizd/docker/Dockerfile-testnet)
- [Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo)
- [docker-main.yml](file://.github/workflows/docker-main.yml)
- [docker-pr-build.yml](file://.github/workflows/docker-pr-build.yml)
- [vizd.sh](file://share/vizd/vizd.sh)
- [config.ini](file://share/vizd/config/config.ini)
- [config_testnet.ini](file://share/vizd/config/config_testnet.ini)
- [config_mongo.ini](file://share/vizd/config/config_mongo.ini)
- [config_debug.ini](file://share/vizd/config/config_debug.ini)
- [snapshot.json](file://share/vizd/snapshot.json)
- [snapshot-testnet.json](file://share/vizd/snapshot-testnet.json)
- [seednodes](file://share/vizd/seednodes)
- [CMakeLists.txt](file://CMakeLists.txt)
</cite>

## Update Summary
**Changes Made**
- Updated GitHub Actions workflow configurations to reflect major version upgrades (actions/checkout v2→v4, docker/build-push-action v1→v6)
- Added docker/login-action@v3 for enhanced authentication in CI/CD pipelines
- Improved error handling and workflow configurations for better build reliability and security
- Enhanced CI/CD pipeline documentation with current best practices

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
This document provides comprehensive Docker integration guidance for the VIZ CPP Node across development and production environments. It covers multi-stage Dockerfiles for production, testnet, low-memory, and MongoDB-enabled builds, the GitHub Actions CI/CD pipeline for automated Docker builds with enhanced security and reliability, container orchestration patterns, volume mounting for persistence, network configuration, environment variable usage, and the relationship between Docker configurations and CMake build options. Practical examples are included for running development containers, connecting to test networks, and deploying production nodes. Security considerations and troubleshooting tips are also provided.

## Project Structure
The Docker integration is centered around four primary Dockerfiles under share/vizd/docker, each tailored to a specific deployment profile. Supporting assets include configuration templates, scripts, snapshots, and seednode lists. The CI/CD pipeline is defined via GitHub Actions workflows with enhanced security and reliability features.

```mermaid
graph TB
subgraph "Docker Build Configurations"
DProd["Dockerfile-production"]
DTest["Dockerfile-testnet"]
DLow["Dockerfile-lowmem"]
DMongo["Dockerfile-mongo"]
end
subgraph "Runtime Assets"
Script["vizd.sh"]
Seed["seednodes"]
Snap["snapshot.json"]
SnapTest["snapshot-testnet.json"]
CfgProd["config.ini"]
CfgTest["config_testnet.ini"]
CfgMongo["config_mongo.ini"]
CfgDebug["config_debug.ini"]
end
subgraph "Enhanced CI/CD"
GHMain[".github/workflows/docker-main.yml<br/>v4 checkout + v6 build-push + v3 login"]
GHPR[".github/workflows/docker-pr-build.yml<br/>v4 checkout + v6 build-push + v3 login"]
end
DProd --> Script
DProd --> Seed
DProd --> Snap
DProd --> CfgProd
DTest --> Script
DTest --> Seed
DTest --> SnapTest
DTest --> CfgTest
DLow --> Script
DLow --> Seed
DLow --> Snap
DLow --> CfgProd
DMongo --> Script
DMongo --> Seed
DMongo --> Snap
DMongo --> CfgMongo
GHMain --> DProd
GHMain --> DTest
GHPR --> DTest
```

**Diagram sources**
- [Dockerfile-production:1-98](file://share/vizd/docker/Dockerfile-production#L1-L98)
- [Dockerfile-testnet:1-98](file://share/vizd/docker/Dockerfile-testnet#L1-L98)
- [Dockerfile-lowmem:1-80](file://share/vizd/docker/Dockerfile-lowmem#L1-L80)
- [Dockerfile-mongo:1-109](file://share/vizd/docker/Dockerfile-mongo#L1-L109)
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)
- [seednodes:1-6](file://share/vizd/seednodes#L1-L6)
- [snapshot.json:1-174](file://share/vizd/snapshot.json#L1-L174)
- [snapshot-testnet.json:1-35](file://share/vizd/snapshot-testnet.json#L1-L35)
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)
- [config_debug.ini:1-126](file://share/vizd/config/config_debug.ini#L1-L126)
- [docker-main.yml:1-53](file://.github/workflows/docker-main.yml#L1-L53)
- [docker-pr-build.yml:1-30](file://.github/workflows/docker-pr-build.yml#L1-L30)

**Section sources**
- [Dockerfile-production:1-98](file://share/vizd/docker/Dockerfile-production#L1-L98)
- [Dockerfile-testnet:1-98](file://share/vizd/docker/Dockerfile-testnet#L1-L98)
- [Dockerfile-lowmem:1-80](file://share/vizd/docker/Dockerfile-lowmem#L1-L80)
- [Dockerfile-mongo:1-109](file://share/vizd/docker/Dockerfile-mongo#L1-L109)
- [docker-main.yml:1-53](file://.github/workflows/docker-main.yml#L1-L53)
- [docker-pr-build.yml:1-30](file://.github/workflows/docker-pr-build.yml#L1-L30)

## Core Components
- Multi-stage Dockerfiles:
  - Production: Builds with standard node settings, exposes RPC and P2P ports, mounts data directories for persistence.
  - Testnet: Similar to production but enables testnet-specific configuration and snapshot.
  - Low-memory: Optimized for constrained environments using a low-memory build option.
  - MongoDB-enabled: Installs MongoDB C/C++ drivers and enables the mongo_db plugin.
- Enhanced CI/CD:
  - Automated Docker builds for master branch (production and testnet) with improved security using docker/login-action@v3.
  - PR builds for production images with ref tagging using latest GitHub Actions versions.
  - Robust error handling and authentication mechanisms for reliable builds.
- Runtime:
  - A service wrapper script initializes configuration, applies optional seed nodes, and starts the node with environment-driven endpoints and optional witness settings.
  - Configuration templates define RPC endpoints, P2P endpoints, plugin sets, logging, and optional MongoDB connection.
  - Snapshot assets enable fast initialization of blockchain data.

Key runtime environment variables supported by the container entrypoint:
- VIZD_SEED_NODES: Comma-separated list of seed nodes to connect to.
- VIZD_WITNESS_NAME: Optional witness name for block production.
- VIZD_PRIVATE_KEY: Private key for witness signing.
- VIZD_RPC_ENDPOINT: Override RPC endpoint binding.
- VIZD_P2P_ENDPOINT: Override P2P endpoint binding.
- VIZD_EXTRA_OPTS: Additional arguments appended to the node command.

Exposed ports:
- 8090: HTTP RPC
- 8091: WebSocket RPC
- 2001: P2P

Volumes:
- /var/lib/vizd: Blockchain data directory
- /etc/vizd: Configuration directory

**Section sources**
- [Dockerfile-production:76-98](file://share/vizd/docker/Dockerfile-production#L76-L98)
- [Dockerfile-testnet:77-98](file://share/vizd/docker/Dockerfile-testnet#L77-L98)
- [Dockerfile-lowmem:58-80](file://share/vizd/docker/Dockerfile-lowmem#L58-L80)
- [Dockerfile-mongo:87-109](file://share/vizd/docker/Dockerfile-mongo#L87-L109)
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)
- [docker-main.yml:1-53](file://.github/workflows/docker-main.yml#L1-L53)
- [docker-pr-build.yml:1-30](file://.github/workflows/docker-pr-build.yml#L1-L30)

## Architecture Overview
The Docker-based deployment architecture separates build-time and runtime concerns with enhanced CI/CD security:
- Build-time: Multi-stage Dockerfiles compile the node with CMake options selected per variant.
- Runtime: A minimal base image runs the node under a service supervisor, with volumes for persistence and configuration overlays.
- CI/CD: Enhanced GitHub Actions workflows with improved authentication, error handling, and build reliability.

```mermaid
graph TB
subgraph "Enhanced Build Stages"
B1["Stage 1: Builder Base<br/>Install deps, clone repo, cmake configure<br/>actions/checkout@v4 + docker/login-action@v3"]
B2["Stage 2: Install Artifacts<br/>make install, cleanup"]
end
subgraph "Enhanced Runtime Image"
R1["Minimal Base"]
R2["Copy /usr/local from builder"]
R3["Add user, cache dirs, service wrapper"]
R4["Mount volumes, expose ports"]
end
B1 --> B2 --> R2 --> R3 --> R4
```

**Diagram sources**
- [Dockerfile-production:1-98](file://share/vizd/docker/Dockerfile-production#L1-L98)
- [Dockerfile-testnet:1-98](file://share/vizd/docker/Dockerfile-testnet#L1-L98)
- [Dockerfile-lowmem:1-80](file://share/vizd/docker/Dockerfile-lowmem#L1-L80)
- [Dockerfile-mongo:1-109](file://share/vizd/docker/Dockerfile-mongo#L1-L109)
- [docker-main.yml:17-21](file://.github/workflows/docker-main.yml#L17-L21)
- [docker-pr-build.yml:15-19](file://.github/workflows/docker-pr-build.yml#L15-L19)

## Detailed Component Analysis

### Dockerfile Variants and CMake Options
Each Dockerfile selects CMake options to tailor the build:
- Production: Standard release build with full plugin set and no special flags.
- Testnet: Enables BUILD_TESTNET and uses testnet configuration and snapshot.
- Low-memory: Enables LOW_MEMORY_NODE to reduce memory footprint.
- MongoDB-enabled: Installs MongoDB C drivers and enables ENABLE_MONGO_PLUGIN.

```mermaid
flowchart TD
Start(["Start Build"]) --> Stage1["Stage 1: Install Build Tools and Dependencies"]
Stage1 --> CopySrc["Copy Source and Configure CMake"]
CopySrc --> CMakeCfg{"Select CMake Options"}
CMakeCfg --> |Production| OptProd["Release, Shared=OFF,<br/>Testnet=OFF, LowMem=OFF, Mongo=OFF"]
CMakeCfg --> |Testnet| OptTest["Release, Shared=OFF,<br/>Testnet=ON, LowMem=OFF, Mongo=OFF"]
CMakeCfg --> |Low-Memory| OptLM["Release, Shared=OFF,<br/>Testnet=OFF, LowMem=ON, Mongo=OFF"]
CMakeCfg --> |MongoDB| OptMongo["Release, Shared=OFF,<br/>Testnet=OFF, LowMem=OFF, Mongo=ON"]
OptProd --> Build["Compile and Install"]
OptTest --> Build
OptLM --> Build
OptMongo --> MongoDeps["Install MongoDB C/C++ Drivers"] --> Build
Build --> Stage2["Stage 2: Runtime Image Setup"]
Stage2 --> End(["Image Ready"])
```

**Diagram sources**
- [Dockerfile-production:51-69](file://share/vizd/docker/Dockerfile-production#L51-L69)
- [Dockerfile-testnet:51-65](file://share/vizd/docker/Dockerfile-testnet#L51-L65)
- [Dockerfile-lowmem:37-51](file://share/vizd/docker/Dockerfile-lowmem#L37-L51)
- [Dockerfile-mongo:66-80](file://share/vizd/docker/Dockerfile-mongo#L66-L80)
- [CMakeLists.txt:56-89](file://CMakeLists.txt#L56-L89)

**Section sources**
- [Dockerfile-production:51-69](file://share/vizd/docker/Dockerfile-production#L51-L69)
- [Dockerfile-testnet:51-65](file://share/vizd/docker/Dockerfile-testnet#L51-L65)
- [Dockerfile-lowmem:37-51](file://share/vizd/docker/Dockerfile-lowmem#L37-L51)
- [Dockerfile-mongo:66-80](file://share/vizd/docker/Dockerfile-mongo#L66-L80)
- [CMakeLists.txt:56-89](file://CMakeLists.txt#L56-L89)

### Enhanced CI/CD Pipeline (GitHub Actions)
Automated Docker builds are configured with enhanced security and reliability:
- Master branch: Builds production and testnet images with improved authentication using docker/login-action@v3 and pushes to the registry with appropriate tags.
- Pull requests: Builds a production image and tags it with the PR ref for review using latest GitHub Actions versions.

**Updated** Enhanced with major version upgrades and improved security measures

```mermaid
sequenceDiagram
participant Dev as "Developer"
participant GH as "GitHub"
participant Act as "Actions Runner"
participant Auth as "docker/login-action@v3"
participant Build as "docker/build-push-action@v6"
participant Reg as "Container Registry"
Dev->>GH : Push to master
GH->>Act : Trigger docker-main.yml
Act->>Auth : Authenticate with Docker Hub
Auth->>Reg : Login with credentials
Act->>Build : Build and push production image (latest)
Build->>Reg : Push with docker/build-push-action@v6
Act->>Build : Build and push testnet image (testnet)
Build->>Reg : Push with docker/build-push-action@v6
Dev->>GH : Open PR
GH->>Act : Trigger docker-pr-build.yml
Act->>Auth : Authenticate with Docker Hub
Auth->>Reg : Login with credentials
Act->>Build : Build and push production image (ref-tagged)
Build->>Reg : Push with docker/build-push-action@v6
```

**Diagram sources**
- [docker-main.yml:17-31](file://.github/workflows/docker-main.yml#L17-L31)
- [docker-pr-build.yml:15-29](file://.github/workflows/docker-pr-build.yml#L15-L29)

**Section sources**
- [docker-main.yml:1-53](file://.github/workflows/docker-main.yml#L1-L53)
- [docker-pr-build.yml:1-30](file://.github/workflows/docker-pr-build.yml#L1-L30)

### Container Runtime and Environment Variables
The container entrypoint script orchestrates node startup:
- Applies default seed nodes from the seednodes file if none are provided via environment.
- Copies the packaged configuration into the data directory and adjusts ownership.
- Optionally replays from a cached snapshot if present.
- Starts the node with configurable RPC and P2P endpoints and optional witness parameters.

```mermaid
sequenceDiagram
participant Entrypoint as "vizd.sh"
participant FS as "Mounted Volumes"
participant Node as "vizd"
Entrypoint->>Entrypoint : Parse env vars (seed, witness, keys, endpoints)
Entrypoint->>FS : Copy /etc/vizd/config.ini -> /var/lib/vizd/config.ini
Entrypoint->>FS : Optionally extract cached snapshot to blockchain dir
Entrypoint->>Node : exec vizd with data-dir, endpoints, plugins, extra opts
Node-->>Entrypoint : stdout/stderr
```

**Diagram sources**
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)

**Section sources**
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)

### Configuration Templates and Plugin Sets
Configuration files define RPC endpoints, plugin sets, logging, and optional MongoDB URI. The testnet configuration enables witness production and includes a default witness and private key suitable for automated testing.

- Production: Full plugin set excluding MongoDB.
- Testnet: Includes witness plugin and default witness credentials.
- MongoDB: Adds mongo_db plugin and a MongoDB URI for external connectivity.

**Section sources**
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)

### Volume Mounting and Persistence
- /var/lib/vizd: Contains blockchain data, logs, and configuration overrides.
- /etc/vizd: Contains initial configuration and seednodes; copied into the data directory at first run.

Practical guidance:
- Bind-mount a host directory to /var/lib/vizd for persistent storage across container restarts.
- Place a custom config.ini into /etc/vizd to override defaults; it will be copied into the data directory on first run.

**Section sources**
- [Dockerfile-production:97-98](file://share/vizd/docker/Dockerfile-production#L97-L98)
- [Dockerfile-testnet:97-98](file://share/vizd/docker/Dockerfile-testnet#L97-L98)
- [Dockerfile-lowmem:79-80](file://share/vizd/docker/Dockerfile-lowmem#L79-L80)
- [Dockerfile-mongo:108-109](file://share/vizd/docker/Dockerfile-mongo#L108-L109)
- [vizd.sh:40-43](file://share/vizd/vizd.sh#L40-L43)

### Network Configuration and Connectivity
- Exposed ports: 8090 (HTTP RPC), 8091 (WebSocket RPC), 2001 (P2P).
- Seed nodes: Provided via /etc/vizd/seednodes; overridden by VIZD_SEED_NODES environment variable.
- P2P endpoint binding: Defaults to 0.0.0.0:2001; overrideable via VIZD_P2P_ENDPOINT.
- RPC endpoint binding: Defaults to 0.0.0.0:8090; overrideable via VIZD_RPC_ENDPOINT.

For MongoDB-enabled deployments, the configuration template includes a MongoDB URI suitable for connecting to a MongoDB instance reachable from the container's network namespace.

**Section sources**
- [Dockerfile-production:89-95](file://share/vizd/docker/Dockerfile-production#L89-L95)
- [Dockerfile-testnet:89-95](file://share/vizd/docker/Dockerfile-testnet#L89-L95)
- [Dockerfile-lowmem:71-77](file://share/vizd/docker/Dockerfile-lowmem#L71-L77)
- [Dockerfile-mongo:100-106](file://share/vizd/docker/Dockerfile-mongo#L100-L106)
- [vizd.sh:62-72](file://share/vizd/vizd.sh#L62-L72)
- [seednodes:1-6](file://share/vizd/seednodes#L1-L6)
- [config_mongo.ini:71-72](file://share/vizd/config/config_mongo.ini#L71-L72)

### Practical Examples

- Run a production node locally:
  - docker run -d \
    --name viz-prod \
    -p 8090:8090 -p 8091:8091 -p 2001:2001 \
    -v /srv/viz/data:/var/lib/vizd \
    -v /srv/viz/etc:/etc/vizd \
    vizblockchain/vizd:latest

- Connect to the testnet:
  - docker run -d \
    --name viz-testnet \
    -e VIZD_SEED_NODES="seed1.testnet.viz:2001,seed2.testnet.viz:2001" \
    -p 8090:8090 -p 8091:8091 -p 2001:2001 \
    -v /srv/viz/testnet-data:/var/lib/vizd \
    vizblockchain/vizd:testnet

- Deploy a MongoDB-enabled node:
  - docker run -d \
    --name viz-mongo \
    -e VIZD_SEED_NODES="..." \
    -p 8090:8090 -p 8091:8091 -p 2001:2001 \
    -v /srv/viz/mongo-data:/var/lib/vizd \
    -v /srv/viz/mongo-etc:/etc/vizd \
    vizblockchain/vizd:mongo

- Run a witness node:
  - docker run -d \
    --name viz-witness \
    -e VIZD_WITNESS_NAME="your-witness" \
    -e VIZD_PRIVATE_KEY="5...your-private-key" \
    -p 8090:8090 -p 8091:8091 -p 2001:2001 \
    -v /srv/viz/witness-data:/var/lib/vizd \
    vizblockchain/vizd:latest

[No sources needed since this section provides practical examples without analyzing specific files]

## Dependency Analysis
The Docker build depends on CMake options to select features and plugins. The CI/CD pipeline depends on Docker Hub credentials and the presence of the Dockerfiles with enhanced authentication security.

**Updated** Enhanced with improved authentication and build action versions

```mermaid
graph LR
CMake["CMakeLists.txt<br/>BUILD_TESTNET / LOW_MEMORY_NODE / ENABLE_MONGO_PLUGIN"]
DP["Dockerfile-production"]
DT["Dockerfile-testnet"]
DL["Dockerfile-lowmem"]
DM["Dockerfile-mongo"]
CMake --> DP
CMake --> DT
CMake --> DL
CMake --> DM
GHMain["docker-main.yml<br/>v4 checkout + v6 build-push + v3 login"] --> DP
GHMain --> DT
GHPR["docker-pr-build.yml<br/>v4 checkout + v6 build-push + v3 login"] --> DT
```

**Diagram sources**
- [CMakeLists.txt:56-89](file://CMakeLists.txt#L56-L89)
- [Dockerfile-production:56-61](file://share/vizd/docker/Dockerfile-production#L56-L61)
- [Dockerfile-testnet:56-62](file://share/vizd/docker/Dockerfile-testnet#L56-L62)
- [Dockerfile-lowmem:43-48](file://share/vizd/docker/Dockerfile-lowmem#L43-L48)
- [Dockerfile-mongo:72-77](file://share/vizd/docker/Dockerfile-mongo#L72-L77)
- [docker-main.yml:17-31](file://.github/workflows/docker-main.yml#L17-L31)
- [docker-pr-build.yml:15-29](file://.github/workflows/docker-pr-build.yml#L15-L29)

**Section sources**
- [CMakeLists.txt:56-89](file://CMakeLists.txt#L56-L89)
- [docker-main.yml:1-53](file://.github/workflows/docker-main.yml#L1-L53)
- [docker-pr-build.yml:1-30](file://.github/workflows/docker-pr-build.yml#L1-L30)

## Performance Considerations
- Multi-stage builds minimize final image size by discarding build tools and build artifacts after installation.
- Using Release builds and disabling shared libraries reduces binary size and improves runtime performance characteristics.
- Low-memory builds reduce shared memory sizing and related overhead for constrained environments.
- MongoDB builds include static drivers to avoid runtime dependency resolution overhead.
- Caching: ccache is enabled during CMake configuration to speed up rebuilds when iterating on changes.
- Enhanced CI/CD: Latest GitHub Actions versions provide improved build performance and reliability.

[No sources needed since this section provides general guidance]

## Troubleshooting Guide
Common issues and resolutions:
- Ports already in use:
  - Change published ports or stop conflicting services.
- Permission denied on data directory:
  - Ensure the container user owns /var/lib/vizd and mounted volumes are writable.
- No connectivity to peers:
  - Verify VIZD_SEED_NODES or rely on default seednodes; confirm firewall/NAT rules allow inbound P2P traffic on port 2001.
- Slow startup due to replay:
  - Provide a cached snapshot or pre-seeded blockchain data in /var/lib/vizd to accelerate synchronization.
- Witness production not starting:
  - Confirm VIZD_WITNESS_NAME and VIZD_PRIVATE_KEY are set and match the configured witness and private key in the configuration.
- MongoDB plugin errors:
  - Ensure the MongoDB URI is reachable from the container network and matches the configuration template.
- CI/CD build failures:
  - Verify Docker Hub credentials are properly configured; check docker-login-action@v3 authentication.
  - Ensure actions/checkout@v4 and docker/build-push-action@v6 are compatible with your repository structure.

Operational checks:
- Inspect container logs for startup errors.
- Verify configuration file presence in /var/lib/vizd after first run.
- Monitor RPC and P2P endpoints availability.
- Check GitHub Actions workflow logs for authentication and build errors.

**Section sources**
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)
- [config_mongo.ini:71-72](file://share/vizd/config/config_mongo.ini#L71-L72)
- [docker-main.yml:21-24](file://.github/workflows/docker-main.yml#L21-L24)
- [docker-pr-build.yml:19-22](file://.github/workflows/docker-pr-build.yml#L19-L22)

## Conclusion
The Docker integration for VIZ CPP Node provides flexible, reproducible deployments across production, testnet, low-memory, and MongoDB-enabled scenarios. Enhanced multi-stage builds, modernized CI/CD automation with improved security and reliability, and robust runtime configuration enable efficient development and operations. By leveraging volumes, environment variables, and configuration templates, teams can deploy reliable nodes with predictable performance and minimal operational overhead. The recent upgrades to GitHub Actions workflows ensure better build reliability and security for automated Docker image creation.

[No sources needed since this section summarizes without analyzing specific files]

## Appendices

### Appendix A: Environment Variables Reference
- VIZD_SEED_NODES: Comma-separated P2P endpoints to bootstrap connections.
- VIZD_WITNESS_NAME: Name of the witness to produce blocks.
- VIZD_PRIVATE_KEY: Private key for witness signing.
- VIZD_RPC_ENDPOINT: RPC endpoint binding (host:port).
- VIZD_P2P_ENDPOINT: P2P endpoint binding (host:port).
- VIZD_EXTRA_OPTS: Additional CLI options appended to the node process.

**Section sources**
- [vizd.sh:17-37](file://share/vizd/vizd.sh#L17-L37)
- [vizd.sh:62-72](file://share/vizd/vizd.sh#L62-L72)

### Appendix B: CMake Options and Docker Variants Mapping
- BUILD_TESTNET: Selects testnet configuration and snapshot.
- LOW_MEMORY_NODE: Reduces memory footprint for constrained environments.
- ENABLE_MONGO_PLUGIN: Enables MongoDB plugin and installs drivers.
- BUILD_SHARED_LIBRARIES=OFF: Produces static binaries for portability.
- CMAKE_BUILD_TYPE=Release: Optimized release build.

**Section sources**
- [CMakeLists.txt:56-89](file://CMakeLists.txt#L56-L89)
- [Dockerfile-production:56-61](file://share/vizd/docker/Dockerfile-production#L56-L61)
- [Dockerfile-testnet:56-62](file://share/vizd/docker/Dockerfile-testnet#L56-L62)
- [Dockerfile-lowmem:43-48](file://share/vizd/docker/Dockerfile-lowmem#L43-L48)
- [Dockerfile-mongo:72-77](file://share/vizd/docker/Dockerfile-mongo#L72-L77)

### Appendix C: Enhanced GitHub Actions Workflow Versions
**Updated** Current workflow versions and security enhancements

- actions/checkout@v4: Latest checkout action with improved performance and security
- docker/login-action@v3: Enhanced authentication with better credential handling
- docker/build-push-action@v6: Latest build and push action with improved reliability
- Error handling: Better failure detection and reporting in CI/CD pipelines
- Authentication: Secure Docker Hub credentials management through GitHub Secrets

**Section sources**
- [docker-main.yml:17-21](file://.github/workflows/docker-main.yml#L17-L21)
- [docker-pr-build.yml:19-22](file://.github/workflows/docker-pr-build.yml#L19-L22)