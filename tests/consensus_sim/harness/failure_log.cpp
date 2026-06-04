#include "failure_log.hpp"

#include <boost/filesystem.hpp>

#include <fstream>

namespace consensus_sim {

namespace bfs = boost::filesystem;

void write_failure_log(const std::string& scenario_name,
                       const scenario_driver& driver) {
    bfs::path dir = bfs::current_path() / "failures";
    boost::system::error_code ec;
    bfs::create_directories(dir, ec);
    if (ec) return;

    auto seed = driver.config().seed;
    auto path = dir / (std::to_string(seed) + "-" + scenario_name + ".log");
    std::ofstream f(path.string());
    if (!f) return;

    f << "seed=" << seed << "\n"
      << "scenario=" << scenario_name << "\n"
      << "num_witnesses=" << driver.config().num_witnesses << "\n"
      << "max_slots=" << driver.config().max_slots << "\n"
      << "events=" << driver.event_log().size() << "\n\n";

    f << "## events\n";
    for (const auto& e : driver.event_log()) f << e << "\n";

    f << "\n## final state\n";
    for (auto* n : driver.nodes()) {
        f << n->label()
          << " head=" << n->head_block_num()
          << " lib=" << n->last_irreversible_block_num() << "\n";
    }

    if (driver.violation()) {
        const auto& v = *driver.violation();
        f << "\n## violation\n"
          << "invariant=" << v.invariant_name << "\n"
          << "detail=" << v.detail << "\n"
          << "node=" << v.node_label << "\n"
          << "block_num=" << v.block_num << "\n";
    }
}

} // namespace consensus_sim
