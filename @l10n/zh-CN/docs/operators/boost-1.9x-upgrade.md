# 升级到 Boost 1.9x 构建

本次发布的节点二进制文件使用 Boost 1.91 构建，而不是 Ubuntu 24.04 自带的
Boost 1.83。

## 我需要重放（replay）吗？

**需要。** 由 Boost 1.83 构建写入的 `shared_memory.bin` 无法被 Boost 1.91
构建复用。这是实测结论，而非假设：由 1.83 二进制创建的段，被其他方面完全相同的
1.91 二进制重新打开时，会在具名对象查找处失败 —— Boost.Interprocess 的段索引在
这两个版本之间并不保证内存布局稳定，因此该状态是真正不可读，而不只是可疑。

现在节点会在启动时直接拒绝这样的状态目录，并同时报告两个版本：

    database was created by a different compiler, build, or operating system.
      state was built with: 13.3.0 boost-1_83
      this binary uses:     13.3.0 boost-1_91
    Replay from the block log or import a snapshot to rebuild state.

对于在此检查*存在之前*写入的状态目录，已存储的一侧会读作
`<unreadable -- predates this check, or a Boost too different to parse>`。
处理方式相同。

要么从区块日志重放，要么导入一份较新的快照。**你的区块日志不受影响** —— 只有
`shared_memory.bin` 会被重建。降级回 1.83 构建是对称的：它同样会拒绝 1.91
的状态并要求重放。

## 从源码构建

Docker 镜像无需任何操作。在原版 Ubuntu 24.04 上从源码构建依然可行：代码树支持
Boost 1.83 到 1.9x，CI 覆盖了该区间的两端。要与官方镜像保持一致，请使用
`link=static` 和 `cxxflags=-std=c++14` 构建 Boost 1.91，或直接把
`vizblockchain/vizd:boost-base-1.91-noble` 镜像用作构建环境：

    docker build -f share/vizd/docker/Dockerfile-boost -t vizblockchain/vizd:boost-base-1.91-noble .

若针对安装在 `/usr/local`（而非通过 apt 安装）的 Boost 进行配置，请传入
`-DBOOST_ROOT=/usr/local`，生产环境和测试网 Dockerfile 都是这样做的。
