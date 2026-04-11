#pragma once

// fc::raw serialization support for chainbase::object_id
// Must be included before fc/io/raw.hpp to ensure ADL finds these operators
// when fc::raw dispatches non-reflected class types via s << v / s >> v

#include <chainbase/chainbase.hpp>

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
