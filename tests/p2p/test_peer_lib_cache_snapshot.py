#!/usr/bin/env python3
"""Exercise the actual cache reader and publisher under a concurrent fake P2P writer."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
header=(root/'libraries/network/include/graphene/network/dlt_p2p_node.hpp').read_text()
source=(root/'libraries/network/dlt_p2p_node.cpp').read_text()
template=(root/'tests/p2p/fixtures/peer_lib_cache_harness.cpp').read_text()
def extract(text,signature):
    start=text.index(signature); pos=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[pos]=='{')-(text[pos]=='}');pos+=1
    return text[start:pos]
getter=extract(header,'peer_lib_view get_peer_lib_view() const noexcept {').replace('peer_lib_view get_peer_lib_view()', 'dlt_p2p_node::peer_lib_view dlt_p2p_node::get_peer_lib_view()',1)
refresh=extract(source,'void dlt_p2p_node::refresh_peer_lib_cache() {')
code=template.replace('// @GETTER@',getter).replace('// @REFRESH@',refresh)
with tempfile.TemporaryDirectory(prefix='viz-peer-lib-cache-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['g++','-std=c++14','-Wall','-Wextra','-Werror','-pthread',str(p/'test.cpp'),'-o',str(p/'test')],check=True,timeout=45)
    subprocess.run([str(p/'test')],check=True,timeout=20)
