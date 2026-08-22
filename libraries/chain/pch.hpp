// Precompiled header for graphene_chain.
//
// Opt-in via -DENABLE_PCH=ON. Lists only *stable* third-party headers (Boost,
// FC, C++ standard library) that are pulled in — directly or transitively — by
// most translation units in this library. Project headers are intentionally
// excluded: they change often, and putting a churning header in the PCH forces
// a full-library rebuild on every edit, defeating the purpose.
//
// Chosen from an include-frequency scan of libraries/chain/*.{hpp,cpp}; the
// dominant cost here is Boost.MultiIndex plus FC serialization/reflection.
#pragma once

// Boost.MultiIndex — the chainbase object indexes instantiate these heavily.
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>

// Boost misc used across the library.
#include <boost/interprocess/containers/flat_set.hpp>
#include <boost/filesystem.hpp>

// FC framework — serialization, reflection, exceptions, primitives.
#include <fc/exception/exception.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/datastream.hpp>
#include <fc/variant.hpp>
#include <fc/uint128_t.hpp>
#include <fc/filesystem.hpp>

// C++ standard library.
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
