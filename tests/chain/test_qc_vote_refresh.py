#!/usr/bin/env python3
"""Execute the production QC tally update with a small deterministic C++ fixture.

The surrounding vote verification and chain state are simulated; this checks the
actual update block, including same-round competing IDs and newer rounds.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'libraries/chain/database.cpp').read_text()
start = source.index('            auto held_qc = _block_qcs.find(block_num);')
end = source.index('            return true;', start)
fragment = source[start:end]
start = source.index('            auto held = _block_qcs.find(cert.block_num);', source.index('void database::remember_block_qc('))
end = source.index('\n        }', start)
remember = source[start:end]
fixture = r'''
#include <algorithm>
#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>
#define ilog(...) ((void)0)
namespace fc { struct duration { long count() const { return 1; } }; struct time_point { static time_point now() { return {}; } }; duration operator-(time_point, time_point) { return {}; } }
struct block_qc { unsigned block_num=0, round=0; std::string block_id; std::vector<std::pair<std::string,int>> signatures; fc::duration formed_after; };
struct votes { std::map<std::string,int> votes; fc::time_point first_seen; };
struct certificate { unsigned block_num=0, round=0; std::string block_id; std::map<std::string,int> signatures; };
struct db {
 std::map<unsigned,block_qc> _block_qcs;
 void update(unsigned block_num, std::string block_id, unsigned round, std::string validator, int signature, struct votes &tally, unsigned quorum) {
 // @UPDATE@
 }
 void remember(const certificate &cert) {
 // @REMEMBER@
 }
};
int main() {
 db d; struct votes t; t.votes={{"a",1},{"b",2},{"c",3}};
 d.update(42,"one",7,"c",3,t,3);
 assert(d._block_qcs.at(42).signatures.size()==3);
 t.votes["d"]=4; d.update(42,"one",7,"d",4,t,3);
 auto &q=d._block_qcs.at(42); assert(q.signatures.size()==4);
 assert(std::find(q.signatures.begin(),q.signatures.end(),std::make_pair(std::string("d"),4))!=q.signatures.end());
 // Simulate one cached signer's key/schedule becoming stale: the additional
 // independent vote lets production re-verification still reach quorum.
 unsigned verified=0; for (const auto &vote:q.signatures) if (vote.first!="a") ++verified;
 assert(verified==3);
 // A conflicting block in the same round must not add votes or replace the QC.
 struct votes rival; rival.votes={{"x",5},{"y",6},{"z",7}};
 d.update(42,"rival",7,"x",5,rival,3);
 assert(q.block_id=="one" && q.signatures.size()==4);
 // A later-round QC replaces it, while an older one does not.
 d.update(42,"later",8,"x",5,rival,3); assert(d._block_qcs.at(42).block_id=="later");
 d.update(42,"old",6,"x",5,rival,3); assert(d._block_qcs.at(42).block_id=="later");
 // A certificate from another source may already exist even if local tally is below quorum.
 db received; received._block_qcs[42]=block_qc{42,7,"one",{{"a",1},{"b",2},{"c",3}},{}};
 struct votes one; one.votes={{"d",4}}; received.update(42,"one",7,"d",4,one,3);
 assert(received._block_qcs.at(42).signatures.size()==4);
 // A second QC for the same block can add signatures, never replace an existing
 // signature (this path also runs during replay where checks can be skipped).
 certificate peer{42,7,"one",{{"a",99},{"e",5}}}; received.remember(peer);
 assert(received._block_qcs.at(42).signatures.size()==5);
 auto &p=received._block_qcs.at(42).signatures;
 assert(std::find(p.begin(),p.end(),std::make_pair(std::string("a"),1))!=p.end());
 certificate enemy{42,7,"other",{{"u",8}}}; received.remember(enemy);
 assert(received._block_qcs.at(42).block_id=="one" && p.size()==5);
 certificate next{42,8,"next",{{"z",8}}}; received.remember(next);
 assert(received._block_qcs.at(42).block_id=="next");
}
'''.replace('// @UPDATE@', fragment).replace('// @REMEMBER@', remember)
with tempfile.TemporaryDirectory(prefix='qc-refresh-') as temp:
    cpp = Path(temp) / 'test.cpp'; cpp.write_text(fixture)
    binary = Path(temp) / 'test'
    subprocess.run(['g++', '-std=c++14', '-Wall', '-Wextra', '-o', str(binary), str(cpp)], check=True)
    subprocess.run([str(binary)], check=True)
print('QC tally refresh: ok (isolated production update block)')
