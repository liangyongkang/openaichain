/**
 *  @file
 *  @copyright defined in oac/LICENSE.txt
 */
#include "oac.system.hpp"

#include <oaclib/oac.hpp>
#include <oaclib/print.hpp>
#include <oaclib/datastream.hpp>
#include <oaclib/serialize.hpp>
#include <oaclib/multi_index.hpp>
#include <oaclib/privileged.h>
#include <oaclib/transaction.hpp>

#include <oac.token/oac.token.hpp>


#include <cmath>
#include <map>

namespace oacsystem {
   using oac::asset;
   using oac::indexed_by;
   using oac::const_mem_fun;
   using oac::bytes;
   using oac::print;
   using oac::permission_level;
   using std::map;
   using std::pair;

   static constexpr time refund_delay = 3*24*3600;
   static constexpr time refund_expiration_time = 3600;

   struct user_resources {
      account_name  owner;
      asset         net_weight;
      asset         cpu_weight;
      int64_t       ram_bytes = 0;

      uint64_t primary_key()const { return owner; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      OACLIB_SERIALIZE( user_resources, (owner)(net_weight)(cpu_weight)(ram_bytes) )
   };


   /**
    *  Every user 'from' has a scope/table that uses every receipient 'to' as the primary key.
    */
   struct delegated_bandwidth {
      account_name  from;
      account_name  to;
      asset         net_weight;
      asset         cpu_weight;

      uint64_t  primary_key()const { return to; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      OACLIB_SERIALIZE( delegated_bandwidth, (from)(to)(net_weight)(cpu_weight) )

   };

   struct refund_request {
      account_name  owner;
      time          request_time;
      oac::asset  net_amount;
      oac::asset  cpu_amount;

      uint64_t  primary_key()const { return owner; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      OACLIB_SERIALIZE( refund_request, (owner)(request_time)(net_amount)(cpu_amount) )
   };

   /**
    *  These tables are designed to be constructed in the scope of the relevant user, this
    *  facilitates simpler API for per-user queries
    */
   typedef oac::multi_index< N(userres), user_resources>      user_resources_table;
   typedef oac::multi_index< N(delband), delegated_bandwidth> del_bandwidth_table;
   typedef oac::multi_index< N(refunds), refund_request>      refunds_table;



   /**
    *  This action will buy an exact amount of ram and bill the payer the current market price.
    */
   void system_contract::buyrambytes( account_name payer, account_name receiver, uint32_t bytes ) {
      auto itr = _rammarket.find(S(4,RAMCORE));
      auto tmp = *itr;
      auto oacout = tmp.convert( asset(bytes,S(0,RAM)), CORE_SYMBOL );

      buyram( payer, receiver, oacout );
   }


   /**
    *  When buying ram the payer irreversiblly transfers quant to system contract and only
    *  the receiver may reclaim the tokens via the sellram action. The receiver pays for the
    *  storage of all database records associated with this action.
    *
    *  RAM is a scarce resource whose supply is defined by global properties max_ram_size. RAM is
    *  priced using the bancor algorithm such that price-per-byte with a constant reserve ratio of 100:1.
    */
   void system_contract::buyram( account_name payer, account_name receiver, asset quant )
   {
      require_auth( payer );
      oac_assert( quant.amount > 0, "must purchase a positive amount" );

      auto fee = quant;
      fee.amount /= 200; /// .5% fee
      auto quant_after_fee = quant;
      quant_after_fee.amount -= fee.amount;

      INLINE_ACTION_SENDER(oac::token, transfer)( N(oac.token), {payer,N(active)},
         { payer, N(oac.ram), quant_after_fee, std::string("buy ram") } );

      if( fee.amount > 0 ) {
         INLINE_ACTION_SENDER(oac::token, transfer)( N(oac.token), {payer,N(active)},
                                                       { payer, N(oac.ramfee), fee, std::string("ram fee") } );
      }

      int64_t bytes_out;

      const auto& market = _rammarket.get(S(4,RAMCORE), "ram market does not exist");
      _rammarket.modify( market, 0, [&]( auto& es ) {
          bytes_out = es.convert( quant_after_fee,  S(0,RAM) ).amount;
      });

      oac_assert( bytes_out > 0, "must reserve a positive amount" );

      _gstate.total_ram_bytes_reserved += uint64_t(bytes_out);
      _gstate.total_ram_stake          += quant_after_fee.amount;

      user_resources_table  userres( _self, receiver );
      auto res_itr = userres.find( receiver );
      if( res_itr ==  userres.end() ) {
         res_itr = userres.emplace( receiver, [&]( auto& res ) {
               res.owner = receiver;
               res.ram_bytes = bytes_out;
            });
      } else {
         userres.modify( res_itr, receiver, [&]( auto& res ) {
               res.ram_bytes += bytes_out;
            });
      }
      set_resource_limits( res_itr->owner, res_itr->ram_bytes, res_itr->net_weight.amount, res_itr->cpu_weight.amount );
   }


   /**
    *  While buying ram uses the current market price according to the bancor-algorithm, selling ram only
    *  refunds the purchase price to the account. In this way there is no profit to be made through buying
    *  and selling ram.
    */
   void system_contract::sellram( account_name account, int64_t bytes ) {
      require_auth( account );
      oac_assert( bytes > 0, "cannot sell negative byte" );

      user_resources_table  userres( _self, account );
      auto res_itr = userres.find( account );
      oac_assert( res_itr != userres.end(), "no resource row" );
      oac_assert( res_itr->ram_bytes >= bytes, "insufficient quota" );

      asset tokens_out;
      auto itr = _rammarket.find(S(4,RAMCORE));
      _rammarket.modify( itr, 0, [&]( auto& es ) {
          /// the cast to int64_t of bytes is safe because we certify bytes is <= quota which is limited by prior purchases
          tokens_out = es.convert( asset(bytes,S(0,RAM)), CORE_SYMBOL);
      });

      _gstate.total_ram_bytes_reserved -= static_cast<decltype(_gstate.total_ram_bytes_reserved)>(bytes); // bytes > 0 is asserted above
      _gstate.total_ram_stake          -= tokens_out.amount;

      //// this shouldn't happen, but just in case it does we should prevent it
      oac_assert( _gstate.total_ram_stake >= 0, "error, attempt to unstake more tokens than previously staked" );

      userres.modify( res_itr, account, [&]( auto& res ) {
          res.ram_bytes -= bytes;
      });
      set_resource_limits( res_itr->owner, res_itr->ram_bytes, res_itr->net_weight.amount, res_itr->cpu_weight.amount );

      INLINE_ACTION_SENDER(oac::token, transfer)( N(oac.token), {N(oac.ram),N(active)},
                                                       { N(oac.ram), account, asset(tokens_out), std::string("sell ram") } );
      auto fee = tokens_out.amount / 200;
      if( fee > 0 ) {
         INLINE_ACTION_SENDER(oac::token, transfer)( N(oac.token), {account,N(active)},
            { account, N(oac.ramfee), asset(fee), std::string("sell ram fee") } );
      }
   }

   void validate_b1_vesting( int64_t stake ) {
      const int64_t base_time = 1527811200; /// 2018-06-01
      const int64_t max_claimable = 100'000'000'0000ll;
      const int64_t claimable = int64_t(max_claimable * double(now()-base_time) / (10*seconds_per_year) );

      oac_assert( max_claimable - claimable <= stake, "b1 can only claim their tokens over 10 years" );
   }

   void system_contract::changebw( account_name from, account_name receiver,
                                   const asset stake_net_delta, const asset stake_cpu_delta, bool transfer )
   {
      require_auth( from );
      oac_assert( stake_net_delta != asset(0) || stake_cpu_delta != asset(0), "should stake non-zero amount" );

      account_name source_stake_from = from;
      if ( transfer ) {
         from = receiver;
      }

      // update stake delegated from "from" to "receiver"
      {
         del_bandwidth_table     del_tbl( _self, from);
         auto itr = del_tbl.find( receiver );
         if( itr == del_tbl.end() ) {
            itr = del_tbl.emplace( from, [&]( auto& dbo ){
                  dbo.from          = from;
                  dbo.to            = receiver;
                  dbo.net_weight    = stake_net_delta;
                  dbo.cpu_weight    = stake_cpu_delta;
               });
         }
         else {
            del_tbl.modify( itr, 0, [&]( auto& dbo ){
                  dbo.net_weight    += stake_net_delta;
                  dbo.cpu_weight    += stake_cpu_delta;
               });
         }
         oac_assert( asset(0) <= itr->net_weight, "insufficient staked net bandwidth" );
         oac_assert( asset(0) <= itr->cpu_weight, "insufficient staked cpu bandwidth" );
         if ( itr->net_weight == asset(0) && itr->cpu_weight == asset(0) ) {
            del_tbl.erase( itr );
         }
      } // itr can be invalid, should go out of scope

      // update totals of "receiver"
      {
         user_resources_table   totals_tbl( _self, receiver );
         auto tot_itr = totals_tbl.find( receiver );
         if( tot_itr ==  totals_tbl.end() ) {
            tot_itr = totals_tbl.emplace( from, [&]( auto& tot ) {
                  tot.owner = receiver;
                  tot.net_weight    = stake_net_delta;
                  tot.cpu_weight    = stake_cpu_delta;
               });
         } else {
            totals_tbl.modify( tot_itr, from == receiver ? from : 0, [&]( auto& tot ) {
                  tot.net_weight    += stake_net_delta;
                  tot.cpu_weight    += stake_cpu_delta;
               });
         }
         oac_assert( asset(0) <= tot_itr->net_weight, "insufficient staked total net bandwidth" );
         oac_assert( asset(0) <= tot_itr->cpu_weight, "insufficient staked total cpu bandwidth" );

         set_resource_limits( receiver, tot_itr->ram_bytes, tot_itr->net_weight.amount, tot_itr->cpu_weight.amount );

         if ( tot_itr->net_weight == asset(0) && tot_itr->cpu_weight == asset(0)  && tot_itr->ram_bytes == 0 ) {
            totals_tbl.erase( tot_itr );
         }
      } // tot_itr can be invalid, should go out of scope

      // create refund or update from existing refund
      if ( N(oac.stake) != source_stake_from ) { //for oac both transfer and refund make no sense
         refunds_table refunds_tbl( _self, from );
         auto req = refunds_tbl.find( from );

         //create/update/delete refund
         auto net_balance = stake_net_delta;
         auto cpu_balance = stake_cpu_delta;
         bool need_deferred_trx = false;
         if ( req != refunds_tbl.end() ) { //need to update refund
            refunds_tbl.modify( req, 0, [&]( refund_request& r ) {
                  if ( net_balance < asset(0) || cpu_balance < asset(0) ) {
                     r.request_time = now();
                  }
                  r.net_amount -= net_balance;
                  if ( r.net_amount < asset(0) ) {
                     net_balance = -r.net_amount;
                     r.net_amount = asset(0);
                  } else {
                     net_balance = asset(0);
                  }
                  r.cpu_amount -= cpu_balance;
                  if ( r.cpu_amount < asset(0) ){
                     cpu_balance = -r.cpu_amount;
                     r.cpu_amount = asset(0);
                  } else {
                     cpu_balance = asset(0);
                  }
               });
            oac_assert( asset(0) <= req->net_amount, "negative net refund amount" ); //should never happen
            oac_assert( asset(0) <= req->cpu_amount, "negative cpu refund amount" ); //should never happen

            if ( req->net_amount == asset(0) && req->cpu_amount == asset(0) ) {
               refunds_tbl.erase( req );
               need_deferred_trx = false;
            } else {
               need_deferred_trx = true;
            }
         } else if ( net_balance < asset(0) || cpu_balance < asset(0) ) { //need to create refund
            refunds_tbl.emplace( from, [&]( refund_request& r ) {
                  r.owner = from;
                  if ( net_balance < asset(0) ) {
                     r.net_amount = -net_balance;
                     net_balance = asset(0);
                  } // else r.net_amount = 0 by default constructor
                  if ( cpu_balance < asset(0) ) {
                     r.cpu_amount = -cpu_balance;
                     cpu_balance = asset(0);
                  } // else r.cpu_amount = 0 by default constructor
                  r.request_time = now();
               });
            need_deferred_trx = true;
         } // else stake increase requested with no existing row in refunds_tbl -> nothing to do with refunds_tbl

         if ( need_deferred_trx ) {
            oac::transaction out;
            out.actions.emplace_back( permission_level{ from, N(active) }, _self, N(refund), from );
            out.delay_sec = refund_delay;
            out.send( from, receiver, true );
         } else {
            cancel_deferred( from );
         }

         auto transfer_amount = net_balance + cpu_balance;
         if ( asset(0) < transfer_amount ) {
            INLINE_ACTION_SENDER(oac::token, transfer)( N(oac.token), {source_stake_from, N(active)},
               { source_stake_from, N(oac.stake), asset(transfer_amount), std::string("stake bandwidth") } );
         }
      }

      // update voting power
      {
         asset total_update = stake_net_delta + stake_cpu_delta;
         auto from_voter = _voters.find(from);
         if( from_voter == _voters.end() ) {
            from_voter = _voters.emplace( from, [&]( auto& v ) {
                  v.owner  = from;
                  v.staked = total_update.amount;
               });
         } else {
            _voters.modify( from_voter, 0, [&]( auto& v ) {
                  v.staked += total_update.amount;
               });
         }
         oac_assert( 0 <= from_voter->staked, "stake for voting cannot be negative");
         if( from == N(b1) ) {
            validate_b1_vesting( from_voter->staked );
         }

         if( from_voter->producers.size() || from_voter->proxy ) {
            update_votes( from, from_voter->proxy, from_voter->producers, false );
         }
      }
   }

   void system_contract::delegatebw( account_name from, account_name receiver,
                                     asset stake_net_quantity,
                                     asset stake_cpu_quantity, bool transfer )
   {
      oac_assert( stake_cpu_quantity >= asset(0), "must stake a positive amount" );
      oac_assert( stake_net_quantity >= asset(0), "must stake a positive amount" );
      oac_assert( stake_net_quantity + stake_cpu_quantity > asset(0), "must stake a positive amount" );

      changebw( from, receiver, stake_net_quantity, stake_cpu_quantity, transfer);
   } // delegatebw

   void system_contract::undelegatebw( account_name from, account_name receiver,
                                       asset unstake_net_quantity, asset unstake_cpu_quantity )
   {
      oac_assert( asset() <= unstake_cpu_quantity, "must unstake a positive amount" );
      oac_assert( asset() <= unstake_net_quantity, "must unstake a positive amount" );
      oac_assert( asset() < unstake_cpu_quantity + unstake_net_quantity, "must unstake a positive amount" );
      oac_assert( _gstate.total_activated_stake >= min_activated_stake,
                    "cannot undelegate bandwidth until the chain is activated (at least 15% of all tokens participate in voting)" );

      changebw( from, receiver, -unstake_net_quantity, -unstake_cpu_quantity, false);
   } // undelegatebw


   void system_contract::refund( const account_name owner ) {
      require_auth( owner );

      refunds_table refunds_tbl( _self, owner );
      auto req = refunds_tbl.find( owner );
      oac_assert( req != refunds_tbl.end(), "refund request not found" );
      oac_assert( req->request_time + refund_delay <= now(), "refund is not available yet" );
      // Until now() becomes NOW, the fact that now() is the timestamp of the previous block could in theory
      // allow people to get their tokens earlier than the 3 day delay if the unstake happened immediately after many
      // consecutive missed blocks.

      INLINE_ACTION_SENDER(oac::token, transfer)( N(oac.token), {N(oac.stake),N(active)},
                                                    { N(oac.stake), req->owner, req->net_amount + req->cpu_amount, std::string("unstake") } );

      refunds_tbl.erase( req );
   }


} //namespace oacsystem
