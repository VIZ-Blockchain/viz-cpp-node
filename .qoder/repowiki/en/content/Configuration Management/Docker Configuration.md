# Docker Configuration

<cite>
**Referenced Files in This Document**
- [Dockerfile-production](file://share/vizd/docker/Dockerfile-production)
- [Dockerfile-testnet](file://share/vizd/docker/Dockerfile-testnet)
- [Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo)
- [vizd.sh](file://share/vizd/vizd.sh)
- [config.ini](file://share/vizd/config/config.ini)
- [config_testnet.ini](file://share/vizd/config/config_testnet.ini)
- [config_mongo.ini](file://share/vizd/config/config_mongo.ini)
- [config_debug.ini](file://share/vizd/config/config_debug.ini)
- [config_debug_mongo.ini](file://share/vizd/config/config_debug_mongo.ini)
- [docker-main.yml](file://.github/workflows/docker-main.yml)
- [docker-pr-build.yml](file://.github/workflows/docker-pr-build.yml)
- [README.md](file://README.md)
</cite>

## Update Summary
**Changes Made**
- Updated compression library dependencies section to reflect libbz2-dev, liblzma-dev, libzstd-dev, and zlib1g-dev additions
- Added documentation for sed command pattern fixes for test subdirectory removal
- Updated base image information to reflect newer phusion/baseimage:noble-1.0.3 variants
- Enhanced dependency analysis to include compression library requirements
- Updated troubleshooting section with compression-related issues

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
This document provides comprehensive Docker configuration guidance for deploying the VIZ C++ Node in containers. It covers available Docker images (production, testnet, low-memory, and MongoDB variants), environment variables, persistent storage via volumes, network exposure, and operational patterns. It also documents image customization, base image selection, security considerations, monitoring and logging, and update procedures.

## Project Structure
The Docker assets and configuration are organized under share/vizd/docker and share/vizd/config. The runtime bootstrap script is located at share/vizd/vizd.sh. Automated builds are defined in GitHub Actions workflows.

```mermaid
graph TB
subgraph "Docker Build Artifacts"
DFProd["Dockerfile-production"]
DFTnet["Dockerfile-testnet"]
DFLowMem["Dockerfile-lowmem"]
DFMongo["Dockerfile-mongo"]
end
subgraph "Runtime Configurations"
CfgProd["config.ini"]
CfgTnet["config_testnet.ini"]
CfgMongo["config_mongo.ini"]
CfgDebug["config_debug.ini"]
CfgDebugMongo["config_debug_mongo.ini"]
end
Script["vizd.sh"]
DFProd --> Script
DFTnet --> Script
DFLowMem --> Script
DFMongo --> Script
DFProd --> CfgProd
DFTnet --> CfgTnet
DFMongo --> CfgMongo
DFLowMem --> CfgProd
```

**Diagram sources**
- [Dockerfile-production:1-103](file://share/vizd/docker/Dockerfile-production#L1-L103)
- [Dockerfile-testnet:1-103](file://share/vizd/docker/Dockerfile-testnet#L1-L103)
- [Dockerfile-lowmem:1-85](file://share/vizd/docker/Dockerfile-lowmem#L1-L85)
- [Dockerfile-mongo:1-114](file://share/vizd/docker/Dockerfile-mongo#L1-L114)
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)

**Section sources**
- [Dockerfile-production:1-103](file://share/vizd/docker/Dockerfile-production#L1-L103)
- [Dockerfile-testnet:1-103](file://share/vizd/docker/Dockerfile-testnet#L1-L103)
- [Dockerfile-lowmem:1-85](file://share/vizd/docker/Dockerfile-lowmem#L1-L85)
- [Dockerfile-mongo:1-114](file://share/vizd/docker/Dockerfile-mongo#L1-L114)
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)

## Core Components
- Production image: Built from Dockerfile-production, targeting the mainnet with standard configuration and exposed ports for RPC and P2P.
- Testnet image: Built from Dockerfile-testnet, configured for testnet with pre-set testnet endpoints and plugins.
- Low-memory image: Built from Dockerfile-lowmem, optimized for constrained environments with reduced memory footprint.
- MongoDB-enabled image: Built from Dockerfile-mongo, including MongoDB drivers and enabling the mongo_db plugin with a default connection URI.

Key runtime behavior is orchestrated by the entrypoint script (vizd.sh), which sets up user permissions, applies environment overrides, initializes optional cached blockchain data, and starts the node with appropriate endpoints and arguments.

**Section sources**
- [Dockerfile-production:81-103](file://share/vizd/docker/Dockerfile-production#L81-L103)
- [Dockerfile-testnet:82-103](file://share/vizd/docker/Dockerfile-testnet#L82-L103)
- [Dockerfile-lowmem:63-85](file://share/vizd/docker/Dockerfile-lowmem#L63-L85)
- [Dockerfile-mongo:92-114](file://share/vizd/docker/Dockerfile-mongo#L92-L114)
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)

## Architecture Overview
The container architecture consists of:
- Base image: phusion/baseimage:noble-1.0.3 variants for Debian-based environments.
- Build stage: installs build dependencies including compression libraries (libbz2-dev, liblzma-dev, libzstd-dev, zlib1g-dev), clones repository, initializes submodules, removes test subdirectories, builds release binaries, and installs artifacts.
- Runtime stage: creates a non-root user, prepares cache and config directories, exposes RPC and P2P ports, defines persistent volumes, and starts the node via an init service.

```mermaid
graph TB
Base["phusion/baseimage:noble-1.0.3<br/>Debian-based runtime"]
Builder["Builder Stage<br/>Install deps, build, install"]
Runtime["Runtime Stage<br/>User, cache, config, expose ports, volumes"]
Builder --> Base
Base --> Runtime
```

**Diagram sources**
- [Dockerfile-production:1-103](file://share/vizd/docker/Dockerfile-production#L1-L103)
- [Dockerfile-testnet:1-103](file://share/vizd/docker/Dockerfile-testnet#L1-L103)
- [Dockerfile-lowmem:1-85](file://share/vizd/docker/Dockerfile-lowmem#L1-L85)
- [Dockerfile-mongo:1-114](file://share/vizd/docker/Dockerfile-mongo#L1-L114)

## Detailed Component Analysis

### Docker Images and Variants
- Production image
  - Purpose: Run on the main VIZ network.
  - Build parameters: standard build with MongoDB disabled.
  - Exposed ports: RPC HTTP (8090), RPC WS (8091), P2P (2001).
  - Persistent volumes: /var/lib/vizd (blockchain data), /etc/vizd (configuration).
  - Entrypoint script: sets defaults and starts node.
  - Reference: [Dockerfile-production:1-103](file://share/vizd/docker/Dockerfile-production#L1-L103), [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98), [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)

- Testnet image
  - Purpose: Run on the testnet.
  - Build parameters: BUILD_TESTNET enabled, MongoDB disabled.
  - Exposed ports: same as production.
  - Persistent volumes: same as production.
  - Entrypoint script: same behavior, with testnet-specific defaults.
  - Reference: [Dockerfile-testnet:1-103](file://share/vizd/docker/Dockerfile-testnet#L1-L103), [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)

- Low-memory image
  - Purpose: Constrained environments.
  - Build parameters: LOW_MEMORY_NODE enabled, MongoDB disabled.
  - Exposed ports: same as production.
  - Persistent volumes: same as production.
  - Reference: [Dockerfile-lowmem:1-85](file://share/vizd/docker/Dockerfile-lowmem#L1-L85)

- MongoDB-enabled image
  - Purpose: Enable historical indexing and analytics via MongoDB.
  - Build parameters: ENABLE_MONGO_PLUGIN enabled, installs MongoDB C/C++ drivers.
  - Exposed ports: same as production.
  - Persistent volumes: same as production.
  - MongoDB URI: configured in the MongoDB-enabled config.
  - Reference: [Dockerfile-mongo:1-114](file://share/vizd/docker/Dockerfile-mongo#L1-L114), [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)

```mermaid
flowchart TD
Start(["Image Build"]) --> DetectVariant{"Variant?"}
DetectVariant --> |Production| ProdBuild["cmake -DENABLE_MONGO_PLUGIN=FALSE<br/>Install artifacts"]
DetectVariant --> |Testnet| TnetBuild["cmake -DBUILD_TESTNET=TRUE<br/>Install artifacts"]
DetectVariant --> |Low-Memory| LowMemBuild["cmake -DLOW_MEMORY_NODE=TRUE<br/>Install artifacts"]
DetectVariant --> |MongoDB| MongoBuild["Install MongoDB drivers<br/>cmake -DENABLE_MONGO_PLUGIN=TRUE<br/>Install artifacts"]
ProdBuild --> RuntimeStage["Prepare user, cache, config<br/>Expose ports, define volumes"]
TnetBuild --> RuntimeStage
LowMemBuild --> RuntimeStage
MongoBuild --> RuntimeStage
RuntimeStage --> Entrypoint["Start via vizd.sh"]
```

**Diagram sources**
- [Dockerfile-production:55-74](file://share/vizd/docker/Dockerfile-production#L55-L74)
- [Dockerfile-testnet:55-74](file://share/vizd/docker/Dockerfile-testnet#L55-L74)
- [Dockerfile-lowmem:41-60](file://share/vizd/docker/Dockerfile-lowmem#L41-L60)
- [Dockerfile-mongo:62-90](file://share/vizd/docker/Dockerfile-mongo#L62-L90)
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)

**Section sources**
- [Dockerfile-production:1-103](file://share/vizd/docker/Dockerfile-production#L1-L103)
- [Dockerfile-testnet:1-103](file://share/vizd/docker/Dockerfile-testnet#L1-L103)
- [Dockerfile-lowmem:1-85](file://share/vizd/docker/Dockerfile-lowmem#L1-L85)
- [Dockerfile-mongo:1-114](file://share/vizd/docker/Dockerfile-mongo#L1-L114)

### Environment Variables
The container supports the following environment variables to customize runtime behavior:
- VIZD_SEED_NODES: Space-delimited list of seed nodes to connect to at startup. Overrides default seed list.
- VIZD_WITNESS_NAME: Name of the witness to operate when block production is enabled.
- VIZD_PRIVATE_KEY: Private key for signing blocks (when operating a witness).
- VIZD_RPC_ENDPOINT: RPC endpoint binding (default: 0.0.0.0:8090).
- VIZD_P2P_ENDPOINT: P2P endpoint binding (default: 0.0.0.0:2001).
- VIZD_EXTRA_OPTS: Additional command-line options appended to the node invocation.

Behavior is implemented in the entrypoint script, which constructs arguments, copies the packaged config into the data directory, and starts the node with the chosen endpoints and optional cached blockchain initialization.

**Section sources**
- [vizd.sh:13-97](file://share/vizd/vizd.sh#L13-L97)
- [config.ini:16-20](file://share/vizd/config/config.ini#L16-L20)
- [config_testnet.ini:16-20](file://share/vizd/config/config_testnet.ini#L16-L20)

### Volume Mounting Strategies
Persistent data storage relies on two primary volumes:
- /var/lib/vizd: Contains blockchain data (including the blockchain directory and cache).
- /etc/vizd: Contains configuration files and seednode lists.

Mounting strategy recommendations:
- Bind mounts for durability and backups: map /var/lib/vizd to a host directory for long-term persistence.
- Config override: mount a host config.ini into /etc/vizd/config.ini to customize RPC endpoints, plugins, and logging without rebuilding images.
- Seednodes override: mount a custom seednodes file into /etc/vizd/seednodes to tailor connectivity.

Exposed ports:
- RPC HTTP: 8090/tcp
- RPC WS: 8091/tcp
- P2P: 2001/tcp

These are defined in the Dockerfiles and used by the entrypoint script.

**Section sources**
- [Dockerfile-production:94-103](file://share/vizd/docker/Dockerfile-production#L94-L103)
- [Dockerfile-testnet:94-103](file://share/vizd/docker/Dockerfile-testnet#L94-L103)
- [Dockerfile-lowmem:76-85](file://share/vizd/docker/Dockerfile-lowmem#L76-L85)
- [Dockerfile-mongo:105-114](file://share/vizd/docker/Dockerfile-mongo#L105-L114)
- [vizd.sh:62-72](file://share/vizd/vizd.sh#L62-L72)

### Network Configuration
- Default RPC endpoints are configurable via environment variables; otherwise, defaults bind to 0.0.0.0 on ports 8090 (HTTP) and 8091 (WS).
- P2P endpoint defaults to 0.0.0.0:2001.
- Port exposure is defined per image; ensure firewall and orchestration platforms allow inbound connections on these ports.
- Seed nodes are either taken from the packaged seednodes file or overridden via VIZD_SEED_NODES.

**Section sources**
- [vizd.sh:62-72](file://share/vizd/vizd.sh#L62-L72)
- [config.ini:1-20](file://share/vizd/config/config.ini#L1-L20)
- [config_testnet.ini:1-20](file://share/vizd/config/config_testnet.ini#L1-L20)

### Image Customization and Base Image Selection
- Base image: phusion/baseimage:noble-1.0.3 variants are used across all images, providing a modern Debian-based runtime environment.
- Build customization:
  - Production: standard build with MongoDB disabled.
  - Testnet: enables BUILD_TESTNET.
  - Low-memory: enables LOW_MEMORY_NODE.
  - MongoDB: installs MongoDB C/C++ drivers and enables ENABLE_MONGO_PLUGIN.
- Debug configurations: separate debug configs are provided for development and MongoDB-enabled debug setups.

**Section sources**
- [Dockerfile-production:1-103](file://share/vizd/docker/Dockerfile-production#L1-L103)
- [Dockerfile-testnet:1-103](file://share/vizd/docker/Dockerfile-testnet#L1-L103)
- [Dockerfile-lowmem:1-85](file://share/vizd/docker/Dockerfile-lowmem#L1-L85)
- [Dockerfile-mongo:1-114](file://share/vizd/docker/Dockerfile-mongo#L1-L114)
- [config_debug.ini:1-135](file://share/vizd/config/config_debug.ini#L1-L135)
- [config_debug_mongo.ini:1-135](file://share/vizd/config/config_debug_mongo.ini#L1-L135)

### Security Considerations
- Non-root execution: the runtime stage creates a dedicated non-root user and sets ownership on cache and data directories.
- Minimal attack surface: images exclude unnecessary packages post-build and rely on minimal base images.
- Secrets handling: private keys and sensitive configuration should be mounted from secure volumes or managed via secret stores in orchestrators.
- Network exposure: restrict inbound access to RPC and P2P ports using firewalls and reverse proxies as appropriate.

**Section sources**
- [Dockerfile-production:84-87](file://share/vizd/docker/Dockerfile-production#L84-L87)
- [Dockerfile-testnet:85-88](file://share/vizd/docker/Dockerfile-testnet#L85-L88)
- [Dockerfile-lowmem:66-69](file://share/vizd/docker/Dockerfile-lowmem#L66-L69)
- [Dockerfile-mongo:95-98](file://share/vizd/docker/Dockerfile-mongo#L95-L98)

### Monitoring and Logging
- Logging configuration is defined in the packaged config files. Logs are written to files under the data directory according to the configured appenders and loggers.
- For production deployments, consider mounting a writable logs directory or integrating with container-native logging stacks.
- Health checks and metrics are not defined in the images; deploy external monitoring and alerting as needed.

**Section sources**
- [config.ini:111-130](file://share/vizd/config/config.ini#L111-L130)
- [config_testnet.ini:113-132](file://share/vizd/config/config_testnet.ini#L113-L132)
- [config_mongo.ini:116-135](file://share/vizd/config/config_mongo.ini#L116-L135)
- [config_debug.ini:107-135](file://share/vizd/config/config_debug.ini#L107-L135)
- [config_debug_mongo.ini:116-135](file://share/vizd/config/config_debug_mongo.ini#L116-L135)

### Practical Deployment Patterns
- Standalone container
  - Run the production image and map ports 8090, 8091, and 2001.
  - Override RPC/P2P endpoints via environment variables if needed.
  - Persist data via a bind mount to /var/lib/vizd.
  - Reference: [README.md:21-29](file://README.md#L21-L29), [Dockerfile-production:94-103](file://share/vizd/docker/Dockerfile-production#L94-L103)

- Testnet node
  - Use the testnet image tag and adjust seed nodes if required.
  - Reference: [README.md:16-20](file://README.md#L16-L20), [Dockerfile-testnet:94-103](file://share/vizd/docker/Dockerfile-testnet#L94-L103)

- Witness node
  - Set VIZD_WITNESS_NAME and VIZD_PRIVATE_KEY to operate a witness.
  - Reference: [vizd.sh:31-37](file://share/vizd/vizd.sh#L31-L37)

- MongoDB analytics node
  - Use the MongoDB-enabled image and configure mongodb-uri in the mounted config.
  - Reference: [Dockerfile-mongo:105-114](file://share/vizd/docker/Dockerfile-mongo#L105-L114), [config_mongo.ini:71-72](file://share/vizd/config/config_mongo.ini#L71-L72)

- Development environment
  - Use debug configurations and enable debug plugins for development and testing.
  - Reference: [config_debug.ini:69-135](file://share/vizd/config/config_debug.ini#L69-L135), [config_debug_mongo.ini:69-135](file://share/vizd/config/config_debug_mongo.ini#L69-L135)

**Section sources**
- [README.md:12-53](file://README.md#L12-L53)
- [Dockerfile-production:94-103](file://share/vizd/docker/Dockerfile-production#L94-L103)
- [Dockerfile-testnet:94-103](file://share/vizd/docker/Dockerfile-testnet#L94-L103)
- [Dockerfile-mongo:105-114](file://share/vizd/docker/Dockerfile-mongo#L105-L114)
- [vizd.sh:31-37](file://share/vizd/vizd.sh#L31-L37)
- [config_debug.ini:69-135](file://share/vizd/config/config_debug.ini#L69-L135)
- [config_debug_mongo.ini:69-135](file://share/vizd/config/config_debug_mongo.ini#L69-L135)

### Container Orchestration and Multi-Container Scenarios
- Single-node deployment: run one container with mapped volumes and ports.
- Multi-node deployment: run multiple containers with distinct data directories and optionally different seed nodes.
- MongoDB stack: when using the MongoDB-enabled image, deploy a MongoDB instance alongside the node container and configure the mongodb-uri accordingly.
- Reverse proxy: front RPC endpoints with a reverse proxy for TLS termination and rate limiting.

### Docker Registry Usage, Versioning, and Updates
- Official image repository: vizblockchain/vizd on Docker Hub.
- Tags:
  - latest: production image built from master.
  - testnet: testnet image built from master.
- Automated builds:
  - Master branch pushes trigger builds for both production and testnet images.
  - Pull requests trigger testnet image builds with ref-based tagging.
- Update procedure:
  - Pull the target tag.
  - Stop the running container.
  - Recreate the container with updated image and preserved volumes.

**Section sources**
- [README.md:14-20](file://README.md#L14-L20)
- [docker-main.yml:1-53](file://.github/workflows/docker-main.yml#L1-L53)
- [docker-pr-build.yml:1-30](file://.github/workflows/docker-pr-build.yml#L1-L30)

## Dependency Analysis
The runtime depends on:
- Entrypoint script for argument construction and startup.
- Configuration files for RPC endpoints, plugins, and logging.
- Persistent volumes for blockchain data and configuration.
- Compression libraries (libbz2-dev, liblzma-dev, libzstd-dev, zlib1g-dev) for efficient blockchain data processing.

**Updated** Added compression library dependencies to support modern blockchain data compression formats.

```mermaid
graph LR
DFProd["Dockerfile-production"] --> Script["vizd.sh"]
DFTnet["Dockerfile-testnet"] --> Script
DFLowMem["Dockerfile-lowmem"] --> Script
DFMongo["Dockerfile-mongo"] --> Script
Script --> CfgProd["config.ini"]
Script --> CfgTnet["config_testnet.ini"]
Script --> CfgMongo["config_mongo.ini"]
```

**Diagram sources**
- [Dockerfile-production:94-96](file://share/vizd/docker/Dockerfile-production#L94-L96)
- [Dockerfile-testnet:95-97](file://share/vizd/docker/Dockerfile-testnet#L95-L97)
- [Dockerfile-mongo:106-108](file://share/vizd/docker/Dockerfile-mongo#L106-L108)
- [vizd.sh:39-42](file://share/vizd/vizd.sh#L39-L42)
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)

**Section sources**
- [vizd.sh:1-98](file://share/vizd/vizd.sh#L1-L98)
- [config.ini:1-130](file://share/vizd/config/config.ini#L1-L130)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)

## Performance Considerations
- Shared memory sizing: tune shared-file-size and related parameters in the configuration to balance memory usage and performance.
- Plugin selection: disable unused plugins to reduce overhead.
- Single write thread: the configuration encourages single-write-thread to mitigate lock contention.
- Low-memory variant: use the low-memory image when running on constrained hardware.
- Compression library optimization: modern compression libraries (libbz2, liblzma, libzstd, zlib) provide efficient blockchain data compression and decompression.

**Updated** Added compression library optimization considerations for improved blockchain data processing performance.

## Troubleshooting Guide
Common issues and resolutions:
- Ports already in use
  - Ensure host ports 8090, 8091, and 2001 are free or remap to different host ports.
  - Verify container port exposure matches published ports.
  - References: [Dockerfile-production:94-103](file://share/vizd/docker/Dockerfile-production#L94-L103), [Dockerfile-testnet:94-103](file://share/vizd/docker/Dockerfile-testnet#L94-L103)

- Permission denied on data directory
  - Confirm the non-root user owns /var/lib/vizd after initial run.
  - Reference: [Dockerfile-production:84-87](file://share/vizd/docker/Dockerfile-production#L84-L87)

- No connectivity to peers
  - Override seed nodes via VIZD_SEED_NODES or mount a custom seednodes file.
  - Reference: [vizd.sh:17-29](file://share/vizd/vizd.sh#L17-L29), [README.md:31-39](file://README.md#L31-L39)

- Blockchain initialization delays
  - Cached blockchain data may be unpacked on first run; allow time for decompression.
  - Reference: [vizd.sh:44-53](file://share/vizd/vizd.sh#L44-L53)

- MongoDB plugin connectivity
  - Ensure mongodb-uri is reachable from the container network and credentials are correct.
  - Reference: [config_mongo.ini:71-72](file://share/vizd/config/config_mongo.ini#L71-L72), [Dockerfile-mongo:105-108](file://share/vizd/docker/Dockerfile-mongo#L105-L108)

- Compression library issues
  - Verify compression libraries (libbz2, liblzma, libzstd, zlib) are properly installed and accessible.
  - Check for compatibility between compression formats and blockchain data versions.
  - Reference: [Dockerfile-production:32-41](file://share/vizd/docker/Dockerfile-production#L32-L41), [Dockerfile-testnet:32-41](file://share/vizd/docker/Dockerfile-testnet#L32-L41)

**Updated** Added compression library troubleshooting section to address potential issues with blockchain data compression.

**Section sources**
- [Dockerfile-production:84-103](file://share/vizd/docker/Dockerfile-production#L84-L103)
- [Dockerfile-testnet:85-103](file://share/vizd/docker/Dockerfile-testnet#L85-L103)
- [vizd.sh:17-53](file://share/vizd/vizd.sh#L17-L53)
- [README.md:31-39](file://README.md#L31-L39)
- [config_mongo.ini:71-72](file://share/vizd/config/config_mongo.ini#L71-L72)

## Conclusion
The VIZ C++ Node provides multiple Docker images tailored for production, testnet, low-memory, and MongoDB-enabled deployments. By leveraging environment variables, persistent volumes, and the packaged configurations, operators can quickly deploy reliable and secure nodes. Automated CI builds maintain official images, and the modular design allows for flexible customization and operational patterns.

## Appendices

### Environment Variable Reference
- VIZD_SEED_NODES: Seed nodes to connect to.
- VIZD_WITNESS_NAME: Witness name for block production.
- VIZD_PRIVATE_KEY: Private key for signing blocks.
- VIZD_RPC_ENDPOINT: RPC endpoint binding.
- VIZD_P2P_ENDPOINT: P2P endpoint binding.
- VIZD_EXTRA_OPTS: Additional CLI options.

**Section sources**
- [vizd.sh:17-97](file://share/vizd/vizd.sh#L17-L97)

### Ports Reference
- RPC HTTP: 8090
- RPC WS: 8091
- P2P: 2001

**Section sources**
- [Dockerfile-production:94-103](file://share/vizd/docker/Dockerfile-production#L94-L103)
- [Dockerfile-testnet:94-103](file://share/vizd/docker/Dockerfile-testnet#L94-L103)
- [Dockerfile-lowmem:76-85](file://share/vizd/docker/Dockerfile-lowmem#L76-L85)
- [Dockerfile-mongo:105-114](file://share/vizd/docker/Dockerfile-mongo#L105-L114)

### Compression Library Dependencies
The Docker images now include modern compression library dependencies:
- libbz2-dev: Bzip2 compression support for blockchain data
- liblzma-dev: LZMA compression support for efficient data compression
- libzstd-dev: Zstandard compression for high-speed compression/decompression
- zlib1g-dev: Standard zlib compression library for compatibility

These libraries are essential for processing modern blockchain data formats and provide optimal compression ratios and performance.

**Section sources**
- [Dockerfile-production:32-41](file://share/vizd/docker/Dockerfile-production#L32-L41)
- [Dockerfile-testnet:32-41](file://share/vizd/docker/Dockerfile-testnet#L32-L41)
- [Dockerfile-lowmem:19-29](file://share/vizd/docker/Dockerfile-lowmem#L19-L29)
- [Dockerfile-mongo:19-29](file://share/vizd/docker/Dockerfile-mongo#L19-L29)

### Sed Command Pattern Fixes
All Dockerfiles now include sed command pattern fixes to remove test subdirectories during the build process:
- `sed -i '/add_subdirectory(tests)/d' thirdparty/fc/CMakeLists.txt`

This ensures that test subdirectories are excluded from compilation, reducing build time and image size while maintaining functionality.

**Section sources**
- [Dockerfile-production](file://share/vizd/docker/Dockerfile-production#L58)
- [Dockerfile-testnet](file://share/vizd/docker/Dockerfile-testnet#L58)
- [Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem#L44)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L73)