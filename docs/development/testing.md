# Testing

VIZ Ledger uses Boost.Test for unit tests and provides utility programs for integration-level testing.

---

## Unit Tests

The unit test binary is built via CMake as part of the `libraries/chain` target.

### Test Categories

| Suite | Description |
|-------|-------------|
| `basic_tests` | Core functionality validation |
| `block_tests` | Blockchain-specific logic |
| `live_tests` | Past hardfork scenario validation |
| `operation_tests` | Operation validation |
| `operation_time_tests` | Time-dependent operations (vesting withdrawals, etc.) |
| `serialization_tests` | Serialization roundtrip checks |

### Running Tests

```bash
# Run all tests
./tests/chain_test

# Filter by suite
./tests/chain_test --run_test=basic_tests

# Filter by specific test case
./tests/chain_test --run_test=basic_tests/my_test_case

# Adjust verbosity
./tests/chain_test --log_level=all --report_level=detailed
```

### Boost.Test Runtime Options

| Option | Values | Description |
|--------|--------|-------------|
| `--log_level` | all, success, test_suite, message, warning, error, cpp_exception, system_error, fatal_error, nothing | Log verbosity |
| `--report_level` | no, confirm, short, detailed | Report detail level |
| `--run_test` | `<suite>` or `<suite>/<test>` | Filter which tests run |

---

## Code Coverage

Enable coverage in a Debug build:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON ..
make chain_test

# Capture baseline
lcov --capture --initial --directory . --output-file base.info

# Run tests
./tests/chain_test

# Capture test tracefile
lcov --capture --directory . --output-file test.info

# Merge and clean
lcov --add-tracefile base.info --add-tracefile test.info --output-file merged.info
lcov --remove merged.info '/usr/*' '*/tests/*' --output-file coverage.info

# Generate HTML report
genhtml coverage.info --output-directory coverage_report
```

---

## Integration Utilities

### Block Log Test Utility

The `test_block_log` utility exercises block storage and retrieval:

```bash
# Built at programs/util/test_block_log
./test_block_log /tmp/test_block_log_dir
```

Opens a block log, appends signed blocks, flushes, and reads them back. Useful for validating block storage logic.

### Transaction Signing Utilities

```bash
# Sign a transaction (JSON input per line)
echo '{"ref_block_num":...}' | ./sign_transaction

# Sign a raw digest
echo '{"digest":"...","wif":"5K..."}' | ./sign_digest
```

Both utilities print computed `digest`, `sig_digest`, signing key, and signature. Useful for diagnosing signing failures by comparing sig_digest against wallet-produced signatures.

---

## Test API Plugin

The `test_api` plugin exposes two JSON-RPC APIs (`test_api_a` and `test_api_b`) for integration tests. It is registered in `programs/vizd/main.cpp` and loaded by the node process.

---

## Testnet Environment

For isolated testing, use the testnet configuration:

```bash
# Start a testnet node
vizd --config share/vizd/config/config_testnet.ini

# Or build a testnet Docker image
docker build -f share/vizd/docker/Dockerfile-testnet -t viz-testnet .
```

A `share/vizd/snapshot-testnet.json` snapshot is available for quick testnet initialization.

---

## Continuous Integration

The CI matrix builds Docker images for multiple variants:

| Variant | Dockerfile |
|---------|-----------|
| Standard | `Dockerfile-production` |
| Testnet | `Dockerfile-testnet` |

Builds trigger per branch and tag, with artifact publishing when credentials are configured.

---

## Writing New Tests

Add new test suites to the existing test target using Boost.Test macros:

```cpp
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(my_feature_tests)

BOOST_AUTO_TEST_CASE(basic_case) {
    BOOST_CHECK_EQUAL(1 + 1, 2);
}

BOOST_AUTO_TEST_SUITE_END()
```

Group tests into the appropriate category suite. Prefer integration tests that hit real chain state over mocked tests to catch divergences between mock and production behavior.

---

See also: [Building](./building.md), [Debugging](./debugging.md), [Plugin Development](./plugin-development.md).
