#pragma once

#include "scenario_driver.hpp"

#include <string>

namespace consensus_sim {

/// Writes `<cwd>/failures/<seed>-<scenario_name>.log` containing the seed,
/// scenario name, config knobs, full event log, final per-node state, and
/// the triggering invariant report (if any). Called from scenarios on
/// violation; no-op-on-disk if the failures/ directory can't be created.
void write_failure_log(const std::string& scenario_name,
                       const scenario_driver& driver);

} // namespace consensus_sim
