#pragma once

// fc::raw serialization support for chainbase types (object_id, shared_string, etc.)
//
// fc::raw dispatches non-reflected class types via s << v / s >> v (ADL).
// The pack/unpack overloads in fc/interprocess/container.hpp are NOT used
// by this code path. We must provide operator<</>> in the type's namespace
// for ADL to find them.

#include <chainbase/chainbase.hpp>
#include <fc/io/raw_fwd.hpp>

// chainbase::object_id - serialize as raw int64_t
namespace chainbase {

    template<typename Stream, typename T>
    inline Stream& operator<<(Stream& s, const object_id<T>& id) {
        s.write((const char*)&id._id, sizeof(id._id));
        return s;
    }

    template<typename Stream, typename T>
    inline Stream& operator>>(Stream& s, object_id<T>& id) {
        s.read((char*)&id._id, sizeof(id._id));
        return s;
    }

} // namespace chainbase

// boost::interprocess types - shared_string, flat_set, vector
// These live in boost::interprocess namespace, so ADL operators go there
namespace boost { namespace interprocess {

    // shared_string (basic_string with interprocess allocator)
    template<typename Stream, typename CharT, typename Traits, typename... A>
    inline Stream& operator<<(Stream& s, const basic_string<CharT, Traits, A...>& v) {
        fc::raw::pack(s, fc::unsigned_int((uint32_t)v.size()));
        if (v.size()) {
            s.write(v.c_str(), v.size());
        }
        return s;
    }

    template<typename Stream, typename CharT, typename Traits, typename... A>
    inline Stream& operator>>(Stream& s, basic_string<CharT, Traits, A...>& v) {
        fc::unsigned_int size;
        fc::raw::unpack(s, size);
        v.resize(size.value);
        if (size.value) {
            s.read(&v[0], size.value);
        }
        return s;
    }

}} // boost::interprocess

// boost::container::flat_set with interprocess allocator
namespace boost { namespace container {

    template<typename Stream, typename T, typename Comp, typename... A>
    inline Stream& operator<<(Stream& s, const flat_set<T, Comp, A...>& v) {
        fc::raw::pack(s, fc::unsigned_int((uint32_t)v.size()));
        for (const auto& item : v) {
            fc::raw::pack(s, item);
        }
        return s;
    }

    template<typename Stream, typename T, typename Comp, typename... A>
    inline Stream& operator>>(Stream& s, flat_set<T, Comp, A...>& v) {
        fc::unsigned_int size;
        fc::raw::unpack(s, size);
        v.clear();
        for (uint32_t i = 0; i < size.value; i++) {
            T tmp;
            fc::raw::unpack(s, tmp);
            v.insert(std::move(tmp));
        }
        return s;
    }

}} // boost::container
