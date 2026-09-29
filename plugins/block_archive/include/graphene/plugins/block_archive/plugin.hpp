#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>

namespace graphene {
namespace plugins {
namespace block_archive {

/**
 * Copies every irreversible block into fixed-size range files under
 * block-archive-dir, so that offline tools can read old blocks after the
 * rolling dlt_block_log has dropped them.
 *
 * Range k holds blocks [k*N, k*N+N-1] (N = block-archive-range, default 10000).
 * A range is written to  <dir>/partial/  and moved to
 *   <dir>/blocks-<first>-<last>.log (+ .log.index)
 * once its last block is irreversible. Files use the dlt_block_log layout
 * (see graphene/chain/dlt_block_log.hpp); <first> is the first block actually
 * stored, so the very first range of a snapshot-started node may be short.
 * Only files in the top directory are complete and immutable.
 */
class plugin final : public appbase::plugin<plugin> {
public:
    APPBASE_PLUGIN_REQUIRES((chain::plugin))

    constexpr const static char *plugin_name = "block_archive";

    static const std::string &name() {
        static std::string name = plugin_name;
        return name;
    }

    plugin();

    ~plugin();

    void set_program_options(
        boost::program_options::options_description &cli,
        boost::program_options::options_description &cfg) override;

    void plugin_initialize(const boost::program_options::variables_map &options) override;

    void plugin_startup() override;

    void plugin_shutdown() override;

private:
    struct plugin_impl;

    std::unique_ptr<plugin_impl> my;
};

} } } // graphene::plugins::block_archive
