#include <boost/algorithm/string.hpp>
#include <graphene/plugins/custom_protocol_api/custom_protocol_api_object.hpp>
#include <graphene/plugins/custom_protocol_api/custom_protocol_api_visitor.hpp>

namespace graphene { namespace plugins { namespace custom_protocol_api {

    operation_visitor::operation_visitor(database& db, uint8_t custom_protocol_store_size)
        : _db(db), _custom_protocol_store_size(custom_protocol_store_size) {
    }

    void operation_visitor::operator()(const custom_operation& op) const {
        std::vector<account_name_type> accounts;
        for (const auto &i : op.required_active_auths) {
            accounts.emplace_back(i);
        }
        for (const auto &i : op.required_regular_auths) {
            accounts.emplace_back(i);
        }
        for (const auto& i : accounts) {
            bool find=false;
            const auto &idx = _db.get_index<custom_protocol_index>().indices().get<by_account_custom_sequence_block_num>();
            auto itr = idx.lower_bound(boost::make_tuple(i, uint64_t(-1) ));
            uint8_t custom_protocol_counter=0;
            while(!find && itr != idx.end() && itr->account == i){
                const auto& custom_protocol_item = *itr;
                ++itr;
                custom_protocol_counter++;
                if(op.id == custom_protocol_item.custom_protocol_id){
                    find=true;
                    _db.modify(custom_protocol_item, [&](custom_protocol_object& c){
                        c.custom_sequence++;
                        c.custom_sequence_block_num = 1 + _db.head_block_num();//head_block_num contains previous block num
                    });
                }
                else{
                    if(custom_protocol_counter>=_custom_protocol_store_size){
                        _db.remove(custom_protocol_item);
                    }
                }
            }
            if(!find){
                _db.create<custom_protocol_object>([&](custom_protocol_object& c) {
                    c.account=i;
                    c.custom_protocol_id=op.id;
                    c.custom_sequence = 1;
                    c.custom_sequence_block_num = 1 + _db.head_block_num();//head_block_num contains previous block num
                });
            }
        }
    }

} } } // graphene::plugins::custom_protocol_api