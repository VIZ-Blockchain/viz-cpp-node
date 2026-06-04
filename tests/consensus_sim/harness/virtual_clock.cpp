#include "virtual_clock.hpp"

#include <stdexcept>

namespace consensus_sim {

virtual_clock::virtual_clock(fc::time_point_sec start) : now_(start) {}

void virtual_clock::advance_to(fc::time_point_sec t) {
    if (t < now_) {
        throw std::logic_error("virtual_clock::advance_to went backward");
    }
    now_ = t;
}

} // namespace consensus_sim
