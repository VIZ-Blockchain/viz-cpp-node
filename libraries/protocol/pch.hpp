// Precompiled header for graphene_protocol.
//
// Opt-in via -DENABLE_PCH=ON. See libraries/chain/pch.hpp for the rationale:
// only stable third-party headers, no project headers. Selected from an
// include-frequency scan of libraries/protocol/*.{hpp,cpp}; the dominant cost
// here is FC reflection / static_variant plus the operation type machinery.
#pragma once

// FC framework — reflection, serialization, primitives, crypto.
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/static_variant.hpp>
#include <fc/exception/exception.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/varint.hpp>
#include <fc/string.hpp>
#include <fc/time.hpp>
#include <fc/optional.hpp>
#include <fc/smart_ref_impl.hpp>
#include <fc/utf8.hpp>
#include <fc/bitutil.hpp>

// C++ standard library.
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
