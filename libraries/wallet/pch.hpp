// Precompiled header for graphene_wallet.
//
// Opt-in via -DENABLE_PCH=ON. See libraries/chain/pch.hpp for the rationale:
// only stable third-party headers, no project headers. Selected from an
// include-frequency scan of libraries/wallet/*.{hpp,cpp}; the cost here is
// Boost.MultiIndex, Boost.Range/algorithm, and FC's api/reflection headers.
#pragma once

// Boost.MultiIndex.
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>

// Boost string / range algorithms used through the wallet.
#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/lexical_cast.hpp>

// FC framework.
#include <fc/api.hpp>
#include <fc/macros.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/base58.hpp>

// C++ standard library.
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <sstream>
#include <iostream>
#include <iterator>
