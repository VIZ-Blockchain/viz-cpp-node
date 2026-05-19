#pragma once

#include <fc/time.hpp>

namespace consensus_sim {

class virtual_clock {
public:
    explicit virtual_clock(fc::time_point_sec start);

    fc::time_point_sec now() const noexcept { return now_; }

    /// Advance virtual time to t. t must be >= now(); throws std::logic_error
    /// otherwise. t == now() is a no-op.
    void advance_to(fc::time_point_sec t);

private:
    fc::time_point_sec now_;
};

} // namespace consensus_sim
