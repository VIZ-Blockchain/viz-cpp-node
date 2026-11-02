#!/usr/bin/env python3
"""Red regression: slow status send must not hold the periodic worker.

Run inside Docker. Uses real extracted heartbeat/broadcast methods and a fake
synchronous send that lasts 200 ms per peer; not a real FC socket/fiber test.
The current draft is expected to fail until bounded asynchronous dispatch exists.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
test = (root / 'tests/p2p/test_periodic_status_broadcast.py').read_text()
test = test.replace('#include <iostream>', '#include <iostream>\n#include <thread>\n#include <chrono>', 1)
send = '        if(_peer_states.count(id)) {\n'
assert test.count(send) == 1
test = test.replace(send, send + '            std::this_thread::sleep_for(std::chrono::milliseconds(200));\n', 1)
call = '    node._delegate=&chain;\n    node.periodic_chain_status();\n    assert((node.delivered=='
assert test.count(call) == 1
test = test.replace(call, '''    node._delegate=&chain;
    const auto start=std::chrono::steady_clock::now();
    node.periodic_chain_status();
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    assert(elapsed < 100 && "periodic task blocked by slow peer");
    assert((node.delivered==''', 1)
with tempfile.TemporaryDirectory(prefix='viz-slow-status-') as directory:
    tmp = Path(directory)
    testdir = tmp / 'tests/p2p'
    srcdir = tmp / 'libraries/network'
    testdir.mkdir(parents=True)
    srcdir.mkdir(parents=True)
    (testdir / 'test_periodic_status_broadcast.py').write_text(test)
    (srcdir / 'dlt_p2p_node.cpp').write_text((root / 'libraries/network/dlt_p2p_node.cpp').read_text())
    sys.exit(subprocess.run([sys.executable, str(testdir / 'test_periodic_status_broadcast.py')]).returncode)
