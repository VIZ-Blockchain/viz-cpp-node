#pragma once

#include <fc/variant_object.hpp>
#include <cstdint>

namespace graphene {
    namespace protocol {

        // 0 selects legacy compile-time cadence; APIs pass the live chain interval.
        fc::variant_object get_config(uint32_t live_block_interval = 0);

    }
} // graphene::protocol
