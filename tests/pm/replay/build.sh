#!/usr/bin/env bash
# Build every replay case. No CMake target, no libfc link, no chain build required —
# see README.md for why this works.
#
#   ./build.sh            build all, into ./out
#   ./build.sh t12_chain  build one
#   BOOST_INC=... ./build.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
# FC_INC override: a git worktree carries no submodules, so point this at the main
# checkout's thirdparty/fc/include when building from one.
: "${FC_INC:=$root/thirdparty/fc/include}"
fc_inc="$FC_INC"
: "${BOOST_INC:=/opt/homebrew/opt/boost/include}"
[ -d "$BOOST_INC" ] || BOOST_INC=/usr/include
: "${CXX:=clang++}"

inc=(-I"$root/libraries/chain/include" -I"$fc_inc" -I"$BOOST_INC")
out="$here/out"; mkdir -p "$out"

# Cases that link the real consensus math. The rest are self-contained (native
# unsigned __int128 stands in for fc::uint128_t) and need only -std=c++17.
linked="t14_f2_ledger dump t2_reach t3_lp t3b_loan t3c_weight t4_cancel t5_threshold t6_cycle t10_conserve t13_penalty"

build_objs() {
    [ -f "$out/parimutuel.o" ] && return 0
    echo "  cc libraries/chain/pm/{parimutuel,leverage}.cpp + fc_u128_shim.cpp"
    $CXX -std=c++14 -w -c "$root/libraries/chain/pm/parimutuel.cpp" -o "$out/parimutuel.o" "${inc[@]}"
    $CXX -std=c++14 -w -c "$root/libraries/chain/pm/leverage.cpp"   -o "$out/leverage.o"   "${inc[@]}"
    $CXX -std=c++14 -w -c "$here/fc_u128_shim.cpp"                  -o "$out/shim.o"       -I"$fc_inc" -I"$BOOST_INC"
}

one() {
    local name="$1" src="$here/$1.cpp"
    [ -f "$src" ] || { echo "no such case: $1" >&2; exit 1; }
    if [[ " $linked " == *" $name "* ]]; then
        build_objs
        $CXX -std=c++14 -w -o "$out/$name" "$src" "$out/parimutuel.o" "$out/leverage.o" "$out/shim.o" "${inc[@]}"
    else
        $CXX -std=c++17 -O1 -w -o "$out/$name" "$src"
    fi
    echo "  built out/$name"
}

if [ $# -gt 0 ]; then
    for n in "$@"; do one "${n%.cpp}"; done
else
    for f in "$here"/t*.cpp "$here"/dump.cpp; do
        [ "$(basename "$f")" = "fc_u128_shim.cpp" ] && continue
        one "$(basename "${f%.cpp}")"
    done
fi
echo "run e.g.: $out/t12_chain"
