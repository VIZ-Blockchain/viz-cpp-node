#pragma once

#include <graphene/protocol/transaction.hpp>
#include <set>

namespace graphene { namespace chain { class database; }
namespace plugins { namespace database_api {

// The implementation invoked by database_api.get_required_signatures.
// Partial available-key sets return their contributions, not an authorization claim.
std::set<graphene::protocol::public_key_type> get_required_signatures_for_api(
    const graphene::chain::database& db,
    const graphene::protocol::signed_transaction& trx,
    const fc::flat_set<graphene::protocol::public_key_type>& available_keys);

} } }
