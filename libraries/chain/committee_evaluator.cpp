#include <graphene/chain/chain_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/committee_objects.hpp>

namespace graphene { namespace chain {

     void committee_worker_create_request_evaluator::do_apply(const committee_worker_create_request_operation& o) {
        const auto& median_props = _db.get_witness_schedule_object().median_props;
        const auto &creator = _db.get_account(o.creator);
        _db.get_account(o.worker);

        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!creator.valid, "Account flagged as invalid");
        if(_db.has_hardfork(CHAIN_HARDFORK_9)){
            FC_ASSERT(creator.balance >=
                      median_props.committee_create_request_fee, "Account does not have sufficient funds to create a committee request: required ${a}.",("a",median_props.committee_create_request_fee));
        }
        bool find=false;

        const auto &idx = _db.get_index<committee_request_index>().indices().get<by_creator_url>();
        auto itr = idx.find(boost::make_tuple(o.creator, o.url));
        if(itr != idx.end()){
            find=true;
            FC_ASSERT(!find, "Committee request with same creator and url already exist.");
        }

        const auto &c_idx = _db.get_index<committee_request_index>().indices().get<by_creator>();
        auto c_itr = c_idx.lower_bound(o.creator);
        while (c_itr != c_idx.end() &&
               c_itr->creator == o.creator) {
            if(COMMITTEE_REQUEST_PER_TIME>(_db.head_block_time() - c_itr->start_time).to_seconds()){
                find=true;
                FC_ASSERT(!find, "Committee request can not be created by same creator before COMMITTEE_REQUEST_PER_TIME ends.");
            }
            ++c_itr;
        }

        if(!find){
            const dynamic_global_property_object &dgp = _db.get_dynamic_global_properties();
            uint32_t committee_request_id = dgp.committee_requests;
            committee_request_id++;
            _db.create<committee_request_object>([&](committee_request_object& c) {
                c.request_id = committee_request_id;
                c.creator = o.creator;
                c.worker = o.worker;
                from_string(c.url, o.url);
                c.required_amount_min = o.required_amount_min;
                c.required_amount_max = o.required_amount_max;
                c.start_time = _db.head_block_time();
                c.duration = o.duration;
                c.end_time = c.start_time + fc::seconds(o.duration);
                c.status = 0;
                c.votes_count = 0;
            });
            _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                dgp.committee_requests = committee_request_id;
            });
            if(_db.has_hardfork(CHAIN_HARDFORK_9)){
                _db.adjust_balance(creator, -median_props.committee_create_request_fee);
                _db.modify(dgp, [&](dynamic_global_property_object &dgp) {
                    dgp.committee_fund += median_props.committee_create_request_fee;
                });
            }
        }
    }

    void committee_worker_cancel_request_evaluator::do_apply(const committee_worker_cancel_request_operation& o) {
        _db.get_account(o.creator);
        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!creator.valid, "Account flagged as invalid");
        const auto &idx = _db.get_index<committee_request_index>().indices().get<by_request_id>();
        auto itr = idx.find(o.request_id);
        FC_ASSERT(itr != idx.end(), "Committee request id not found.");

        if(itr->creator!=o.creator)
            FC_ASSERT(false, "Committee request can canceled only by creator.");

        if(itr->status == 0){
            _db.modify(*itr, [&](committee_request_object& c) {
                c.status = 1;
                c.conclusion_time = _db.head_block_time();
            });
        }
        else{
            FC_ASSERT(false, "Committee request is not active.");
        }
    }

    void committee_vote_request_evaluator::do_apply(const committee_vote_request_operation& o) {
        _db.get_account(o.voter);
        //if(_db.has_hardfork(CHAIN_HARDFORK_9))//can be deleted after fix in CHAIN_HARDFORK_11
        //    FC_ASSERT(!voter.valid, "Account flagged as invalid");
        const auto &idx = _db.get_index<committee_request_index>().indices().get<by_request_id>();
        auto itr = idx.find(o.request_id);
        FC_ASSERT(itr != idx.end(), "Committee request id not found.");

        if(itr->status == 0){
            bool find=false;
            const auto &vote_idx = _db.get_index<committee_vote_index>().indices().get<by_request_id>();
            auto vote_itr = vote_idx.lower_bound(itr->request_id);
            while (vote_itr != vote_idx.end() &&
                   vote_itr->request_id == itr->request_id) {
                const auto &cur_vote = *vote_itr;
                ++vote_itr;
                if(cur_vote.voter==o.voter){
                    find = true;
                    FC_ASSERT(cur_vote.vote_percent != o.vote_percent, "Committee vote percent equal last vote.");
                    _db.modify(cur_vote, [&](committee_vote_object &c) {
                        c.vote_percent = o.vote_percent;
                        c.last_update = _db.head_block_time();
                    });
                }
            }
            if(!find){
                _db.create<committee_vote_object>([&](committee_vote_object& c) {
                    c.request_id = itr->request_id;
                    c.voter = o.voter;
                    c.vote_percent = o.vote_percent;
                    c.last_update = _db.head_block_time();
                });
                _db.modify(*itr, [&](committee_request_object& c) {
                    c.votes_count++;
                });
            }
        }
        else{
            FC_ASSERT(false, "Committee request is not active.");
        }
    }

} } // graphene::chain