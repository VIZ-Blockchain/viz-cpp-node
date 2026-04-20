#pragma once

#include <graphene/chain/database.hpp>

/*
 * This file provides with() functions which modify the database
 * temporarily, then restore it.  These functions are mostly internal
 * implementation detail of the database.
 *
 * Essentially, we want to be able to use "finally" to restore the
 * database regardless of whether an exception is thrown or not, but there
 * is no "finally" in C++.  Instead, C++ requires us to create a struct
 * and put the finally block in a destructor.  Aagh!
 */

namespace graphene {
    namespace chain {
        namespace detail {
            /**
             * Class used to help the without_pending_transactions implementation.
               *
             * TODO:  Change the name of this class to better reflect the fact
             * that it restores popped transactions as well as pending transactions.
             */
            struct pending_transactions_restorer final {
                pending_transactions_restorer(
                    database &db, uint32_t skip,
                    std::vector<signed_transaction> &&pending_transactions
                )
                    : _db(db),
                      _skip(skip),
                      _pending_transactions(std::move(pending_transactions))
                {
                    _db.clear_pending();
                }

                ~pending_transactions_restorer() {
                    auto start = fc::time_point::now();
                    bool apply_trxs = true;
                    uint32_t applied_txs = 0;
                    uint32_t postponed_txs = 0;
                    for (const auto &tx : _db._popped_tx) {
                        if( apply_trxs && fc::time_point::now() - start > CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT ) apply_trxs = false;

                        if( apply_trxs )
                        {
                            try {
                                if( !_db.is_known_transaction( tx.id() ) ) {
                                    // since push_transaction() takes a signed_transaction,
                                    // the operation_results field will be ignored.
                                    _db._push_transaction( tx, _skip );
                                    applied_txs++;
                                }
                            } catch ( const fc::exception&  ) {}
                        }
                        else
                        {
                            _db._pending_tx.push_back( tx );
                            postponed_txs++;
                        }
                    }
                    _db._popped_tx.clear();
                    for (const signed_transaction &tx : _pending_transactions) {
                        if( apply_trxs && fc::time_point::now() - start > CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT ) apply_trxs = false;

                        if( apply_trxs ) {
                            try{
                                if( !_db.is_known_transaction( tx.id() ) ) {
                                    // since push_transaction() takes a signed_transaction,
                                    // the operation_results field will be ignored.
                                    _db._push_transaction( tx, _skip );
                                    applied_txs++;
                                }
                            }
                            catch( const transaction_exception& e )
                            {
                                dlog( "Pending transaction became invalid after switching to block ${b} ${n} ${t}",
                                    ("b", _db.head_block_id())("n", _db.head_block_num())("t", _db.head_block_time()) );
                                dlog( "The invalid transaction caused exception ${e}", ("e", e.to_detail_string()) );
                                dlog( "${t}", ("t", tx) );
                            }
                            catch( const fc::exception& e )
                            {
                                /*
                                dlog( "Pending transaction became invalid after switching to block ${b} ${n} ${t}",
                                    ("b", _db.head_block_id())("n", _db.head_block_num())("t", _db.head_block_time()) );
                                dlog( "The invalid pending transaction caused exception ${e}", ("e", e.to_detail_string() ) );
                                dlog( "${t}", ("t", tx) );
                                */
                            }
                        }
                        else{
                            _db._pending_tx.push_back( tx );
                            postponed_txs++;
                        }
                    }
                    if( postponed_txs > 0 ) {
                        wlog( "Postponed ${p} pending transactions. ${a} were applied.", ("p", postponed_txs)("a", applied_txs) );
                    }
                }

                database &_db;
                uint32_t _skip;
                std::vector<signed_transaction> _pending_transactions;
            };

            /**
             * Class is used to help the with_producing implementation
             */
            struct producing_helper final {
                producing_helper(database& db): _db(db) {
                    _db.set_producing(true);
                }

                ~producing_helper() {
                    _db.set_producing(false);
                }

                database &_db;
            };

            /**
             * Empty pending_transactions, call callback,
             * then reset pending_transactions after callback is done.
             *
             * Pending transactions which no longer validate will be culled.
             */
            template<typename Lambda>
            void without_pending_transactions(
                database& db,
                uint32_t skip,
                std::vector<signed_transaction>&& pending_transactions,
                Lambda callback
            ) {
                pending_transactions_restorer restorer(db, skip, std::move(pending_transactions));
                callback();
                return;
            }

            /**
             * Set producing flag to true, call callback, then set producing flag to false.
             */
             template <typename Lambda>
             void with_producing(
                 database& db,
                 Lambda callback
             ) {
                 producing_helper restorer(db);
                 callback();
             }
        }
    }
} // graphene::chain::detail
