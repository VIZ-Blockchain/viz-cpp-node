#include <graphene/plugins/database_api/signature_discovery.hpp>
#include <graphene/chain/agent_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>

namespace graphene { namespace plugins { namespace database_api {
using namespace graphene::chain;
using namespace graphene::protocol;

std::set<public_key_type> get_required_signatures_for_api(
    const database& db, const signed_transaction& trx,
    const flat_set<public_key_type>& available_keys) {
    const auto get_active = [&](const account_name_type& n) {
        return authority(db.get<account_authority_object, by_account>(n).active);
    };
    const auto get_master = [&](const account_name_type& n) {
        return authority(db.get<account_authority_object, by_account>(n).master);
    };
    const auto get_regular = [&](const account_name_type& n) {
        return authority(db.get<account_authority_object, by_account>(n).regular);
    };
    if (!db.has_hardfork(CHAIN_HARDFORK_15))
        return trx.get_required_signatures(CHAIN_ID, available_keys,
                                           get_active, get_master, get_regular,
                                           CHAIN_MAX_SIG_CHECK_DEPTH);

    flat_set<public_key_type> candidate = available_keys;
    const auto signed_keys = trx.get_signature_keys(CHAIN_ID);
    candidate.insert(signed_keys.begin(), signed_keys.end());
    flat_set<public_key_type> used;
    std::set<public_key_type> result;
    try {
        verify_agent_transaction(db, trx, candidate, true, &used);
        for (const auto& key : used)
            if (available_keys.count(key)) result.insert(key);
        return result;
    } catch (const tx_missing_active_auth&) {
    } catch (const tx_missing_regular_auth&) {
    } catch (const tx_missing_master_auth&) {
    } catch (const tx_missing_other_auth&) {
    }
    // Discovery does not assert full authorization. Keep ordinary partial multisig keys.
    result = trx.get_required_signatures(CHAIN_ID, available_keys,
                                         get_active, get_master, get_regular,
                                         CHAIN_MAX_SIG_CHECK_DEPTH);
    // Agent keys can cover one operation while another operation remains incomplete.
    for (const auto& op : trx.operations) {
        signed_transaction one;
        one.operations.push_back(op);
        try {
            verify_agent_transaction(db, one, candidate, true, &used);
            for (const auto& key : used)
                if (available_keys.count(key)) result.insert(key);
        } catch (const tx_missing_active_auth&) {
        } catch (const tx_missing_regular_auth&) {
        } catch (const tx_missing_master_auth&) {
        } catch (const tx_missing_other_auth&) {
        }
    }
    return result;
}

} } }
