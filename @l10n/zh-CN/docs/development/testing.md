# 测试

VIZ Ledger 使用 Boost.Test 进行单元测试，并提供用于集成级测试的实用程序。

---

## 单元测试

单元测试二进制文件通过 CMake 作为 `libraries/chain` 目标的一部分构建。

### 测试类别

| 套件 | 描述 |
|------|------|
| `basic_tests` | 核心功能验证 |
| `block_tests` | 区块链特定逻辑 |
| `live_tests` | 过去硬分叉场景验证 |
| `operation_tests` | 操作验证 |
| `operation_time_tests` | 时间相关操作（锁仓提取等） |
| `serialization_tests` | 序列化往返检查 |

### 运行测试

```bash
# 运行所有测试
./tests/chain_test

# 按套件过滤
./tests/chain_test --run_test=basic_tests

# 按特定测试用例过滤
./tests/chain_test --run_test=basic_tests/my_test_case

# 调整详细程度
./tests/chain_test --log_level=all --report_level=detailed
```

### Boost.Test 运行时选项

| 选项 | 值 | 描述 |
|------|-----|------|
| `--log_level` | all, success, test_suite, message, warning, error, cpp_exception, system_error, fatal_error, nothing | 日志详细程度 |
| `--report_level` | no, confirm, short, detailed | 报告详细程度 |
| `--run_test` | `<suite>` 或 `<suite>/<test>` | 过滤运行的测试 |

---

## 代码覆盖率

在 Debug 构建中启用覆盖率：

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON ..
make chain_test

# 捕获基线
lcov --capture --initial --directory . --output-file base.info

# 运行测试
./tests/chain_test

# 捕获测试跟踪文件
lcov --capture --directory . --output-file test.info

# 合并并清理
lcov --add-tracefile base.info --add-tracefile test.info --output-file merged.info
lcov --remove merged.info '/usr/*' '*/tests/*' --output-file coverage.info

# 生成 HTML 报告
genhtml coverage.info --output-directory coverage_report
```

---

## 集成工具

### 区块日志测试工具

`test_block_log` 工具测试区块存储和检索：

```bash
# 构建位置：programs/util/test_block_log
./test_block_log /tmp/test_block_log_dir
```

打开区块日志，附加签名区块，刷新，然后读取回来。用于验证区块存储逻辑。

### 交易签名工具

```bash
# 签名交易（每行 JSON 输入）
echo '{"ref_block_num":...}' | ./sign_transaction

# 签名原始摘要
echo '{"digest":"...","wif":"5K..."}' | ./sign_digest
```

两个工具都输出计算得出的 `digest`、`sig_digest`、签名密钥和签名。通过将 sig_digest 与钱包生成的签名进行比较，可诊断签名失败。

---

## 测试 API 插件

`test_api` 插件为集成测试公开两个 JSON-RPC API（`test_api_a` 和 `test_api_b`）。在 `programs/vizd/main.cpp` 中注册，由节点进程加载。

---

## 测试网环境

用于隔离测试，使用 testnet 配置：

```bash
# 启动 testnet 节点
vizd --config share/vizd/config/config_testnet.ini

# 或构建 testnet Docker 镜像
docker build -f share/vizd/docker/Dockerfile-testnet -t viz-testnet .
```

`share/vizd/snapshot-testnet.json` 快照可用于快速初始化 testnet。

---

## 持续集成

CI 矩阵为多种变体构建 Docker 镜像：

| 变体 | Dockerfile |
|------|-----------|
| 标准 | `Dockerfile-production` |
| Testnet | `Dockerfile-testnet` |

按分支和标签触发构建，配置凭据后发布构建产物。

---

## 编写新测试

使用 Boost.Test 宏将新测试套件添加到现有测试目标：

```cpp
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(my_feature_tests)

BOOST_AUTO_TEST_CASE(basic_case) {
    BOOST_CHECK_EQUAL(1 + 1, 2);
}

BOOST_AUTO_TEST_SUITE_END()
```

将测试归入适当的类别套件。优先使用命中真实链状态的集成测试，而非模拟测试，以发现模拟与生产行为之间的差异。

---

参见：[构建](./building.md)、[调试](./debugging.md)、[插件开发](./plugin-development.md)。
