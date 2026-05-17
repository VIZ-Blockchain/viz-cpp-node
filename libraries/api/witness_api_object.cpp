#include <graphene/api/witness_api_object.hpp>

namespace graphene { namespace api {
    validator_api_object::validator_api_object(const validator_object &w, const database& db)
        : id(w.id), owner(w.owner), created(w.created),
          url(to_string(w.url)), total_missed(w.total_missed), last_aslot(w.last_aslot),
          last_confirmed_block_num(w.last_confirmed_block_num),
          signing_key(w.signing_key), props(w.props, db), votes(w.votes),
          penalty_percent(w.penalty_percent), counted_votes(w.counted_votes),
          virtual_last_update(w.virtual_last_update), virtual_position(w.virtual_position),
          virtual_scheduled_time(w.virtual_scheduled_time), last_work(w.last_work),
          running_version(w.running_version), hardfork_version_vote(w.hardfork_version_vote),
          hardfork_time_vote(w.hardfork_time_vote),
          sharing_rate(w.sharing_rate),
          pending_stakeholder_reward(w.pending_stakeholder_reward) {
    }
} } // graphene::api