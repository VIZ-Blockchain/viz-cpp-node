# 构建

VIZ Ledger 节点使用 CMake 3.16+，需要带 coroutine 组件的 Boost 1.71+。支持的平台：Ubuntu 24.04+、macOS（Homebrew）、Windows（MSVC 或 MinGW）。

---

## Linux（Ubuntu/Debian）

### 第一步：安装依赖（需要 root 权限）

```bash
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

安装内容：CMake、GCC/G++、Git、Make、ccache、OpenSSL、Boost 1.71（所有必需组件，包括 coroutine/context）、readline 及压缩库。

### 第二步：构建（以普通用户身份运行，不要用 root）

```bash
chmod +x build-linux.sh
./build-linux.sh
```

**常用选项：**

```bash
./build-linux.sh              # Release 构建（默认）
./build-linux.sh -l           # LOW_MEMORY_NODE（验证者节点）
./build-linux.sh -n           # Testnet 构建
./build-linux.sh -t Debug -j4 # Debug 构建，4 个并行任务
./build-linux.sh --skip-deps  # 跳过依赖安装
./build-linux.sh --install    # 构建后安装到系统

# 自定义依赖路径
./build-linux.sh --boost-root /opt/boost_1_74_0 --openssl-root /opt/openssl
```

### Fedora/RHEL

相同脚本自动检测 `dnf`。安装的包：`cmake`、`gcc-c++`、`git`、`ccache`、`boost-devel`、`openssl-devel`、`bzip2-devel`、`zstd-devel`。

---

## macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

需要 Xcode 命令行工具和 Homebrew。脚本安装：`boost`、`cmake`、`git`、`autoconf`、`automake`、`libtool`、`openssl`、`readline`。

**选项：**

```bash
./build-mac.sh -l              # 低内存节点
./build-mac.sh -n              # Testnet
./build-mac.sh --skip-deps     # 跳过 Homebrew 安装
./build-mac.sh --boost-root /opt/boost_1_74_0
```

---

## Windows（MinGW）

设置必需的环境变量，然后运行批处理脚本：

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

**可选变量：**

| 变量 | 默认值 | 描述 |
|------|--------|------|
| `VIZ_BUILD_TYPE` | Release | Release 或 Debug |
| `VIZ_LOW_MEMORY` | OFF | 启用低内存节点 |
| `VIZ_BUILD_TESTNET` | OFF | Testnet 构建 |
| `VIZ_FULL_STATIC` | OFF | 完全静态二进制文件 |
| `VIZ_CMAKE_EXTRA` | — | 附加 CMake 标志 |

**要求：** MinGW-w64（带 C++11 和 SSE4.2）、CMake 3.16+、Boost 1.71+（静态，`link=static threading=multi runtime-link=shared`）、Windows 版 OpenSSL。

---

## Windows（MSVC）

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-msvc.bat
```

**可选变量：**

| 变量 | 默认值 | 描述 |
|------|--------|------|
| `VIZ_VS_VERSION` | "Visual Studio 17 2022" | Visual Studio 生成器 |
| `VIZ_BUILD_TYPE` | Release | 构建类型 |
| `VIZ_LOW_MEMORY` | OFF | 低内存节点 |
| `VIZ_BUILD_TESTNET` | OFF | Testnet 构建 |

**要求：** Visual Studio 2019+（带"Desktop development with C++"工作负载）、CMake 3.16+。

---

## Docker

仓库附带多种配置的 Dockerfile：

| Dockerfile | 描述 |
|-----------|------|
| `Dockerfile-production` | 完整主网节点（Release） |
| `Dockerfile-testnet` | Testnet（`BUILD_TESTNET=ON`） |

所有 Dockerfile 使用两阶段构建以最小化镜像大小，并使用 Boost 1.71 包（`libboost-coroutine-dev`、`libboost-context-dev`）。

---

## CMake 选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `BUILD_TESTNET` | OFF | 为 testnet 构建 |
| `LOW_MEMORY_NODE` | OFF | 排除非共识数据（减少 RAM） |
| `CHAINBASE_CHECK_LOCKING` | OFF | 启用锁检查（仅用于开发） |
| `BUILD_SHARED_LIBRARIES` | OFF | 构建共享库 |
| `USE_PCH` | OFF | 启用预编译头文件（加速重新构建） |

---

## 高级用法：`configure_build.py`

封装 CMake，提供合理的默认值和交叉编译支持：

```bash
# Release 构建
python3 programs/build_helpers/configure_build.py --release --src ../..

# 带低内存的 Debug
python3 programs/build_helpers/configure_build.py --debug --low-memory

# 使用 MinGW 交叉编译 Windows 版本
python3 programs/build_helpers/configure_build.py --win --release

# 自定义依赖路径
python3 programs/build_helpers/configure_build.py \
  --boost-dir /opt/boost_1_74_0 \
  --openssl-dir /opt/openssl \
  --release
```

---

## 新插件脚手架

```bash
python3 programs/util/newplugin.py graphene myplugin
```

在 `libraries/plugins/myplugin/` 下生成：`CMakeLists.txt`、插件头文件/实现、API 头文件/实现。

---

## 构建目标

| 二进制文件 | 描述 |
|-----------|------|
| `vizd` | 主节点守护程序 |
| `cli_wallet` | 命令行钱包 |
| `js_operation_serializer` | JavaScript 操作序列化器 |
| `size_checker` | 大小分析工具 |

---

## 故障排查

**Boost 版本低于 1.71：** 从包管理器安装 Boost 1.71+（Ubuntu 24.04 附带 1.74）。在 macOS 上，`brew install boost` 提供最新版本。在 Windows 上，从源码构建并包含 coroutine 组件。

**`Do not run this script as root` 错误：** 使用 `sudo ./install-deps-linux.sh` 安装依赖，然后以普通用户身份运行 `./build-linux.sh`。

**缺少 coroutine 组件：** 确保在 Ubuntu/Debian 上安装了 `libboost-coroutine-dev` 和 `libboost-context-dev`。

**macOS OpenSSL 未找到：** 手动设置 `OPENSSL_ROOT_DIR`：`export OPENSSL_ROOT_DIR=$(brew --prefix openssl)`。

**Windows MinGW 缺少变量：** 运行 `build-mingw.bat` 前必须设置 `BOOST_ROOT` 和 `OPENSSL_ROOT_DIR`。

---

参见：[插件开发](./plugin-development.md)、[测试](./testing.md)、[插件概述](../plugins/overview.md)。
