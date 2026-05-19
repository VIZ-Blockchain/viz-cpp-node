
#pragma once

#include <graphene/protocol/operation_util.hpp>

#include <fc/static_variant.hpp>

namespace fc {
    using namespace graphene::protocol;

    std::string name_from_type(const std::string &type_name);
    std::string resolve_operation_name(const std::string &name);

    struct from_operation {
        variant &var;

        from_operation(variant &dv)
                : var(dv) {
        }

        typedef void result_type;

        template<typename T>
        void operator()(const T &v) const {
            auto name = name_from_type(fc::get_typename<T>::name());
            var = variant(std::make_pair(name, v));
        }
    };

    struct get_operation_name {
        std::string &name;

        get_operation_name(std::string &dv)
                : name(dv) {
        }

        typedef void result_type;

        template<typename T>
        void operator()(const T &v) const {
            name = name_from_type(fc::get_typename<T>::name());
        }
    };
}

namespace graphene {
    namespace protocol {

        struct operation_validate_visitor {
            typedef void result_type;

            template<typename T>
            void operator()(const T &v) const {
                v.validate();
            }
        };

        struct operation_get_required_auth_visitor {
            typedef void result_type;

            flat_set <account_name_type> &active;
            flat_set <account_name_type> &master;
            flat_set <account_name_type> &regular;
            std::vector<authority> &other;

            operation_get_required_auth_visitor(
                    flat_set <account_name_type> &a,
                    flat_set <account_name_type> &own,
                    flat_set <account_name_type> &post,
                    std::vector<authority> &oth)
                    : active(a), master(own), regular(post), other(oth) {
            }

            template<typename T>
            void operator()(const T &v) const {
                v.get_required_active_authorities(active);
                v.get_required_master_authorities(master);
                v.get_required_regular_authorities(regular);
                v.get_required_authorities(other);
            }
        };

    }
} // graphene::protocol

//
// Place DEFINE_OPERATION_TYPE in a .cpp file to define
// functions related to your operation type
//
#define DEFINE_OPERATION_TYPE(OperationType)                             \
namespace fc {                                                             \
                                                                           \
void to_variant( const OperationType& var,  fc::variant& vo )              \
{                                                                          \
   var.visit( from_operation( vo ) );                                      \
}                                                                          \
                                                                           \
void from_variant( const fc::variant& var,  OperationType& vo )            \
{                                                                          \
   static std::map<std::string,uint32_t> to_tag = []()                          \
   {                                                                       \
      std::map<std::string,uint32_t> name_map;                                  \
      for( int i = 0; i < OperationType::count(); ++i )                    \
      {                                                                    \
         OperationType tmp;                                                \
         tmp.set_which(i);                                                 \
         std::string n;                                                         \
         tmp.visit( get_operation_name(n) );                               \
         name_map[n] = i;                                                  \
      }                                                                    \
      return name_map;                                                     \
   }();                                                                    \
                                                                           \
   auto ar = var.get_array();                                              \
   if( ar.size() < 2 ) return;                                             \
   if( ar[0].is_uint64() )                                                 \
      vo.set_which( ar[0].as_uint64() );                                   \
   else                                                                    \
   {                                                                       \
      auto resolved = resolve_operation_name(ar[0].as_string());            \
      auto itr = to_tag.find(resolved);                                     \
      FC_ASSERT( itr != to_tag.end(), "Invalid operation name: ${n}", ("n", ar[0]) ); \
      vo.set_which( to_tag[resolved] );                                     \
   }                                                                       \
      vo.visit( fc::to_static_variant( ar[1] ) );                          \
   }                                                                       \
}                                                                          \
                                                                           \
namespace graphene { namespace protocol {                                      \
                                                                           \
void operation_validate( const OperationType& op )                         \
{                                                                          \
   op.visit( graphene::protocol::operation_validate_visitor() );               \
}                                                                          \
                                                                           \
void operation_get_required_authorities( const OperationType& op,          \
                                         flat_set< account_name_type >& active,         \
                                         flat_set< account_name_type >& master,         \
                                         flat_set< account_name_type >& regular,        \
                                         std::vector< authority >& other )     \
{                                                                          \
   op.visit( graphene::protocol::operation_get_required_auth_visitor( active, master, regular, other ) ); \
}                                                                          \
                                                                           \
} } /* graphene::protocol */
