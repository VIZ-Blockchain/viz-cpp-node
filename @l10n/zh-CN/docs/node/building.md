# 从源码构建

VIZ Ledger 使用基于 CMake 的构建系统，为每个平台提供专用构建脚本。Linux 的两步流程（`install-deps-linux.sh` + `build-linux.sh`）将依赖安装（需要 root 权限）与实际构建（以普通用户运行）分离。

---

## 要求

| 组件 | 版本 |
|------|------|
| CMake | 3.16+ |
| GCC | 4.8+ |
| Clang | 3.3+ |
| Boost | 1.71+（含 `coroutine` 组件） |
| OpenSSL | 任意近期版本 |

---

## Linux（Ubuntu 20.04 / 22.04 / 24.04）

### 步骤 1 — 克隆仓库

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
```

### 步骤 2 — 安装依赖（以 root 身份）

```bash
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

安装内容：cmake、gcc/g++、git、boost（包括 coroutine/context 在内的所有组件）、openssl、readline、ccache 及压缩库。

### 步骤 3 — 构建（以普通用户身份）

```bash
chmod +x build-linux.sh
./build-linux.sh
```

输出二进制文件：`build/programs/vizd/vizd`

### 常用构建标志

```bash
# 低内存节点（验证者/种子节点 — 排除历史索引）
./build-linux.sh -l

# 测试网构建
./build-linux.sh -n

# Debug 构建
./build-linux.sh -t Debug

# 并行任务数
./build-linux.sh -j 8

# 跳过依赖安装（已安装）
./build-linux.sh --skip-deps

# 自定义 Boost / OpenSSL 路径
./build-linux.sh --boost-root /opt/boost_1_74_0 --openssl-root /opt/openssl
```

---

## macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

脚本自动安装 Xcode Command Line Tools（如需要）和 Homebrew 依赖，然后配置并构建。

```bash
# 指定 Boost 路径
./build-mac.sh --boost-root /opt/homebrew/opt/boost

# 跳过依赖安装
./build-mac.sh --skip-deps
```

---

## Windows（MinGW）

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

可选环境变量：

| 变量 | 默认值 | 描述 |
|------|-------|------|
| `VIZ_BUILD_TYPE` | `Release` | `Release` 或 `Debug` |
| `VIZ_LOW_MEMORY` | `OFF` | `ON` 构建低内存节点 |
| `VIZ_BUILD_TESTNET` | `OFF` | `ON` 用于测试网构建 |
| `VIZ_FULL_STATIC` | `OFF` | `ON` 构建完全静态二进制文件 |

---

## Windows（MSVC）

需要 Visual Studio 2019+ 及"使用 C++ 的桌面开发"工作负载：

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-msvc.bat
```

---

## CMake 选项

直接使用 CMake 的高级配置：

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `BUILD_TESTNET` | `OFF` | 启用测试网专用代码 |
| `LOW_MEMORY_NODE` | `OFF` | 排除历史/索引插件 |
| `CHAINBASE_CHECK_LOCKING` | `OFF` | 启用锁断言检查（debug） |
| `ENABLE_MONGO_PLUGIN` | `OFF` | 构建 MongoDB 插件 |
| `BUILD_SHARED_LIBRARIES` | `OFF` | 构建共享库 |
| `USE_PCH` | `OFF` | 启用预编译头文件（加快重新构建） |

示例：

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DLOW_MEMORY_NODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
```

或使用辅助脚本：

```bash
python3 programs/build_helpers/configure_build.py --release
```

---

## 构建目标

| 目标 | 二进制文件 | 描述 |
|------|-----------|------|
| `vizd` | `programs/vizd/vizd` | 主节点守护进程 |
| `cli_wallet` | `programs/cli_wallet/cli_wallet` | 命令行钱包 |

---

## Docker 构建

仓库提供四个 Dockerfile：

| 文件 | 用途 |
|------|------|
| `Dockerfile-production` | 完整主网节点（Release） |
| `Dockerfile-lowmem` | 低内存节点（`LOW_MEMORY_NODE=ON`） |
| `Dockerfile-mongo` | 含 MongoDB 插件的节点 |
| `Dockerfile-testnet` | 测试网节点（`BUILD_TESTNET=ON`） |

构建示例：

```bash
docker build -f share/vizd/docker/Dockerfile-production -t vizd:local .
```

完整生产 Docker 设置参见 [Docker](./docker.md)。

---

## 故障排除

| 问题 | 解决方案 |
|------|---------|
| `boost/coroutine.hpp` 未找到 | 安装 `libboost-coroutine-dev`（Ubuntu）或 Boost 1.71+ |
| CMake < 3.16 | 从 `cmake.org` 或 Kitware PPA 安装新版 CMake |
| `do not run as root` 错误 | 以普通用户（非 `sudo`）运行 `build-linux.sh` |
| macOS 链接错误（OpenSSL） | `export OPENSSL_ROOT_DIR=$(brew --prefix openssl)` |
| 编译时内存不足 | 使用 `-j 2` 减少并行任务数 |
