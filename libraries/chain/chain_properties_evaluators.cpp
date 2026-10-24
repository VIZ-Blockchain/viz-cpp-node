#include <graphene/chain/chain_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>

namespace graphene { namespace chain {

    void validator_update_evaluator::do_apply(const validator_update_operation& o) {
        const auto& owner = _db.get_account(o.owner); // verify owner exists

        const auto &idx = _db.get_index<validator_index>().indices().get<by_name>();
        auto itr = idx.find(o.owner);
        if (itr != idx.end()) {
            _db.modify(*itr, [&](validator_object& w) {
                from_string(w.url, o.url);
                w.signing_key = o.block_signing_key;
            });
        } else {
            if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                const auto& median_props = _db.get_validator_schedule_object().median_props;
                const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();

                FC_ASSERT(owner.balance >=
                          median_props.validator_declaration_fee, "Account does not have sufficient funds to declare himself as validator: required ${a}.",("a",median_props.validator_declaration_fee));

                _db.adjust_balance(owner, -median_props.validator_declaration_fee);
                _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                    dgp.committee_fund += median_props.validator_declaration_fee;
                });
            }
            _db.create<validator_object>([&](validator_object& w) {
                w.owner = o.owner;
                from_string(w.url, o.url);
                w.signing_key = o.block_signing_key;
                w.created = _db.head_block_time();
            });
        }
    }

    void chain_properties_update_evaluator::do_apply(const chain_properties_update_operation& o) {
        _db.get_account(o.owner); // verify owner exists

        const auto &idx = _db.get_index<validator_index>().indices().get<by_name>();
        auto itr = idx.find(o.owner);
        if (itr != idx.end()) {
            _db.modify(*itr, [&](validator_object& w) {
                w.props = o.props;
            });
        } else {
            _db.create<validator_object>([&](validator_object& w) {
                w.owner = o.owner;
                w.created = _db.head_block_time();
                w.props = o.props;
            });
        }
    }

    struct chain_properties_update {
        using result_type = void;

        const database& _db;
        chain_properties& _wprops;

        chain_properties_update(const database& db, chain_properties& wprops)
                : _db(db), _wprops(wprops) {
        }

        result_type operator()(const chain_properties_hf4& p) const {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_4), "chain_properties_hf4" );
            _wprops = p;
        }

        result_type operator()(const chain_properties_hf6& p) const {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_6), "chain_properties_hf6" );
            _wprops = p;
        }

        result_type operator()(const chain_properties_hf9& p) const {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_9), "chain_properties_hf9" );
            _wprops = p;
        }

        result_type operator()(const chain_properties_hf13& p) const {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_13), "chain_properties_hf13" );
            _wprops = p;
        }

        result_type operator()(const chain_properties_pm& p) const {
            FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_14), "chain_properties_pm" );
            _wprops = p;
        }

        template<typename Props>
        result_type operator()(Props&& p) const {
            _wprops = p;
        }
    };

    void versioned_chain_properties_update_evaluator::do_apply(const versioned_chain_properties_update_operation& o) {
        FC_ASSERT( _db.has_hardfork(CHAIN_HARDFORK_4), "versioned_chain_properties_update_evaluator not enabled until HF 4" );
        _db.get_account(o.owner); // verify owner exists

        // #6737: protocol validate() allows up to 3 years of 1 s blocks; until HF16 the old
        // one-year-of-3-s-blocks bound stays consensus (the stored props are checked after the
        // visit, so a rejected op leaves nothing behind - the whole op is rolled back).
        const auto check_year_bound = [&](const chain_properties& p) {
            const bool hf16 = _db.has_hardfork(CHAIN_HARDFORK_16);
            const uint64_t reveal_blocks = hf16
                ? uint64_t(p.pm_batch_epoch_blocks) + p.pm_reveal_window_blocks
                : uint64_t(uint32_t(p.pm_batch_epoch_blocks + p.pm_reveal_window_blocks));
            FC_ASSERT(p.pm_closed_market_retention_sec > reveal_blocks * _db.block_interval(),
                      "retention must exceed the live worst-case commit reveal deadline");
            if (_db.has_hardfork(CHAIN_HARDFORK_16)) {
                FC_ASSERT(p.distribution_epoch_length >=
                          CHAIN_MIN_DISTRIBUTION_EPOCH_LENGTH * (CHAIN_BLOCK_INTERVAL / _db.block_interval()),
                          "distribution_epoch_length must preserve the minimum epoch duration after HF16");
                return;
            }
            FC_ASSERT(p.inflation_recalc_period <= CHAIN_BLOCKS_PER_YEAR,
                "inflation_recalc_period must not exceed one year in blocks before HF16");
            FC_ASSERT(p.distribution_epoch_length <= uint32_t(CHAIN_BLOCKS_PER_YEAR),
                "distribution_epoch_length must not exceed one year in blocks before HF16");
        };

        const auto &idx = _db.get_index<validator_index>().indices().get<by_name>();
        auto itr = idx.find(o.owner);
        // Validate a detached candidate. A throwing multi_index modifier may erase
        // the live object before transaction undo can restore it.
        chain_properties candidate = itr != idx.end() ? itr->props : chain_properties();
        o.props.visit(chain_properties_update(_db, candidate));
        check_year_bound(candidate);
        if (itr != idx.end()) {
            _db.modify(*itr, [&](validator_object& w) { w.props = candidate; });
        } else {
            _db.create<validator_object>([&](validator_object& w) {
                w.owner = o.owner;
                w.created = _db.head_block_time();
                w.props = candidate;
            });
        }
    }

    void set_reward_sharing_evaluator::do_apply(const set_reward_sharing_operation& o) {
        ASSERT_REQ_HF(CHAIN_HARDFORK_13, "set_reward_sharing_operation");
        _db.get_account(o.owner); // verify account exists

        const auto& idx = _db.get_index<validator_index>().indices().get<by_name>();
        auto itr = idx.find(o.owner);
        FC_ASSERT(itr != idx.end(), "Account ${a} is not a registered validator", ("a", o.owner));

        _db.modify(*itr, [&](validator_object& w) {
            w.sharing_rate = o.sharing_rate;
        });
    }

} } // graphene::chain