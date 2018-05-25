/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
  
#include <graphene/chain/database.hpp>
#include <graphene/chain/db_with.hpp>
  
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/gravity_emission_object.hpp>
#include <graphene/chain/gravity_activity_object.hpp>
  
#include <graphene/chain/protocol/fee_schedule.hpp>
  
#include <fc/uint128.hpp>
  
#include <time.h>
#include <cmath>
  
namespace graphene { namespace chain {
  
void database::update_global_dynamic_data( const signed_block& b )
{
   const dynamic_global_property_object& _dgp =
      dynamic_global_property_id_type(0)(*this);
  
   uint32_t missed_blocks = get_slot_at_time( b.timestamp );
   assert( missed_blocks != 0 );
   missed_blocks--;
   for( uint32_t i = 0; i < missed_blocks; ++i ) {
      const auto& witness_missed = get_scheduled_witness( i+1 )(*this);
      if(  witness_missed.id != b.witness ) {
         /*
         const auto& witness_account = witness_missed.witness_account(*this);
         if( (fc::time_point::now() - b.timestamp) < fc::seconds(30) )
            wlog( "Witness ${name} missed block ${n} around ${t}", ("name",witness_account.name)("n",b.block_num())("t",b.timestamp) );
            */
  
         modify( witness_missed, [&]( witness_object& w ) {
           w.total_missed++;
         });
      } 
   }
  
   // dynamic global properties updating
   modify( _dgp, [&]( dynamic_global_property_object& dgp ){
      if( BOOST_UNLIKELY( b.block_num() == 1 ) )
         dgp.recently_missed_count = 0;
         else if( _checkpoints.size() && _checkpoints.rbegin()->first >= b.block_num() )
         dgp.recently_missed_count = 0;
      else if( missed_blocks )
         dgp.recently_missed_count += GRAPHENE_RECENTLY_MISSED_COUNT_INCREMENT*missed_blocks;
      else if( dgp.recently_missed_count > GRAPHENE_RECENTLY_MISSED_COUNT_INCREMENT )
         dgp.recently_missed_count -= GRAPHENE_RECENTLY_MISSED_COUNT_DECREMENT;
      else if( dgp.recently_missed_count > 0 )
         dgp.recently_missed_count--;
  
      dgp.head_block_number = b.block_num();
      dgp.head_block_id = b.id();
      dgp.time = b.timestamp;
      dgp.current_witness = b.witness;
      dgp.recent_slots_filled = (
           (dgp.recent_slots_filled << 1)
           + 1) << missed_blocks;
      dgp.current_aslot += missed_blocks+1;
   });
  
   if( !(get_node_properties().skip_flags & skip_undo_history_check) )
   {
      GRAPHENE_ASSERT( _dgp.head_block_number - _dgp.last_irreversible_block_num  < GRAPHENE_MAX_UNDO_HISTORY, undo_database_exception,
                 "The database does not have enough undo history to support a blockchain with so many missed blocks. "
                 "Please add a checkpoint if you would like to continue applying blocks beyond this point.",
                 ("last_irreversible_block_num",_dgp.last_irreversible_block_num)("head", _dgp.head_block_number)
                 ("recently_missed",_dgp.recently_missed_count)("max_undo",GRAPHENE_MAX_UNDO_HISTORY) );
   }
  
   _undo_db.set_max_size( _dgp.head_block_number - _dgp.last_irreversible_block_num + 1 );
   _fork_db.set_max_size( _dgp.head_block_number - _dgp.last_irreversible_block_num + 1 );
}
  
void database::update_signing_witness(const witness_object& signing_witness, const signed_block& new_block)
{
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   uint64_t new_block_aslot = dpo.current_aslot + get_slot_at_time( new_block.timestamp );
  
   share_type witness_pay = std::min( gpo.parameters.witness_pay_per_block, dpo.witness_budget );
  
   modify( dpo, [&]( dynamic_global_property_object& _dpo )
   {
      _dpo.witness_budget -= witness_pay;
   } );
  
   deposit_witness_pay( signing_witness, witness_pay );
  
   modify( signing_witness, [&]( witness_object& _wit )
   {
      _wit.last_aslot = new_block_aslot;
      _wit.last_confirmed_block_num = new_block.block_num();
   } );
}
  
void database::update_last_irreversible_block()
{
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
  
   vector< const witness_object* > wit_objs;
   wit_objs.reserve( gpo.active_witnesses.size() );
   for( const witness_id_type& wid : gpo.active_witnesses )
      wit_objs.push_back( &(wid(*this)) );
  
   static_assert( GRAPHENE_IRREVERSIBLE_THRESHOLD > 0, "irreversible threshold must be nonzero" );
  
   // 1 1 1 2 2 2 2 2 2 2 -> 2     .7*10 = 7
   // 1 1 1 1 1 1 1 2 2 2 -> 1
   // 3 3 3 3 3 3 3 3 3 3 -> 3
  
   size_t offset = ((GRAPHENE_100_PERCENT - GRAPHENE_IRREVERSIBLE_THRESHOLD) * wit_objs.size() / GRAPHENE_100_PERCENT);
  
   std::nth_element( wit_objs.begin(), wit_objs.begin() + offset, wit_objs.end(),
      []( const witness_object* a, const witness_object* b )
      {
         return a->last_confirmed_block_num < b->last_confirmed_block_num;
      } );
  
   uint32_t new_last_irreversible_block_num = wit_objs[offset]->last_confirmed_block_num;
  
   if( new_last_irreversible_block_num > dpo.last_irreversible_block_num )
   {
      modify( dpo, [&]( dynamic_global_property_object& _dpo )
      {
         _dpo.last_irreversible_block_num = new_last_irreversible_block_num;
      } );
   }
}
  
void database::clear_expired_transactions()
{ try {
   //Look for expired transactions in the deduplication list, and remove them.
   //Transactions must have expired by at least two forking windows in order to be removed.
   auto& transaction_idx = static_cast<transaction_index&>(get_mutable_index(implementation_ids, impl_transaction_object_type));
   const auto& dedupe_index = transaction_idx.indices().get<by_expiration>();
   while( (!dedupe_index.empty()) && (head_block_time() > dedupe_index.begin()->trx.expiration) )
      transaction_idx.remove(*dedupe_index.begin());
} FC_CAPTURE_AND_RETHROW() }
  
void database::clear_expired_proposals()
{
   const auto& proposal_expiration_index = get_index_type<proposal_index>().indices().get<by_expiration>();
   while( !proposal_expiration_index.empty() && proposal_expiration_index.begin()->expiration_time <= head_block_time() )
   {
      const proposal_object& proposal = *proposal_expiration_index.begin();
      processed_transaction result;
      try {
         if( proposal.is_authorized_to_execute(*this) )
         {
            result = push_proposal(proposal);
            //TODO: Do something with result so plugins can process it.
            continue;
         }
      } catch( const fc::exception& e ) {
         elog("Failed to apply proposed transaction on its expiration. Deleting it.\n${proposal}\n${error}",
              ("proposal", proposal)("error", e.to_detail_string()));
      }
      remove(proposal);
   }
}
  
/**
 *  let HB = the highest bid for the collateral  (aka who will pay the most DEBT for the least collateral)
 *  let SP = current median feed's Settlement Price 
 *  let LC = the least collateralized call order's swan price (debt/collateral)
 *
 *  If there is no valid price feed or no bids then there is no black swan.
 *
 *  A black swan occurs if MAX(HB,SP) <= LC
 */
bool database::check_for_blackswan( const asset_object& mia, bool enable_black_swan )
{
    if( !mia.is_market_issued() ) return false;
  
    const asset_bitasset_data_object& bitasset = mia.bitasset_data(*this);
    if( bitasset.has_settlement() ) return true; // already force settled
    auto settle_price = bitasset.current_feed.settlement_price;
    if( settle_price.is_null() ) return false; // no feed
  
    const call_order_index& call_index = get_index_type<call_order_index>();
    const auto& call_price_index = call_index.indices().get<by_price>();
  
    const limit_order_index& limit_index = get_index_type<limit_order_index>();
    const auto& limit_price_index = limit_index.indices().get<by_price>();
  
    // looking for limit orders selling the most USD for the least CORE
    auto highest_possible_bid = price::max( mia.id, bitasset.options.short_backing_asset );
    // stop when limit orders are selling too little USD for too much CORE
    auto lowest_possible_bid  = price::min( mia.id, bitasset.options.short_backing_asset );
  
    assert( highest_possible_bid.base.asset_id == lowest_possible_bid.base.asset_id );
    // NOTE limit_price_index is sorted from greatest to least
    auto limit_itr = limit_price_index.lower_bound( highest_possible_bid );
    auto limit_end = limit_price_index.upper_bound( lowest_possible_bid );
  
    auto call_min = price::min( bitasset.options.short_backing_asset, mia.id );
    auto call_max = price::max( bitasset.options.short_backing_asset, mia.id );
    auto call_itr = call_price_index.lower_bound( call_min );
    auto call_end = call_price_index.upper_bound( call_max );
  
    if( call_itr == call_end ) return false;  // no call orders
  
    price highest = settle_price;
    if( limit_itr != limit_end ) {
       assert( settle_price.base.asset_id == limit_itr->sell_price.base.asset_id );
       highest = std::max( limit_itr->sell_price, settle_price );
    }
  
    auto least_collateral = call_itr->collateralization();
    if( ~least_collateral >= highest  ) 
    {
       elog( "Black Swan detected: \n"
             "   Least collateralized call: ${lc}  ${~lc}\n"
           //  "   Highest Bid:               ${hb}  ${~hb}\n"
             "   Settle Price:              ${sp}  ${~sp}\n"
             "   Max:                       ${h}   ${~h}\n",
            ("lc",least_collateral.to_real())("~lc",(~least_collateral).to_real())
          //  ("hb",limit_itr->sell_price.to_real())("~hb",(~limit_itr->sell_price).to_real())
            ("sp",settle_price.to_real())("~sp",(~settle_price).to_real())
            ("h",highest.to_real())("~h",(~highest).to_real()) );
       FC_ASSERT( enable_black_swan, "Black swan was detected during a margin update which is not allowed to trigger a blackswan" );
       globally_settle_asset(mia, ~least_collateral );
       return true;
    } 
    return false;
}

inline std::string Long2dt()
{
time_t t = time(NULL);
 struct tm *tm = localtime(&t);
 char date[20];  
 strftime(date, sizeof(date), "%d-%m-%Y %H:%M:%S", tm);
 std::string str(date);
 return str;
}

void database:: process_gravity_emission( const uint32_t& block_num )
{
  if( ( block_num -_last_emission_processing_time ) >= get_global_properties().parameters.emission_period )
  {
   std::cout << "||*********************************** processing gravity_emission ***********************************||" << std::endl;
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
    
    auto activity_weight = get_global_properties().parameters.activity_weight;
    const asset_object& core = asset_id_type(0)(*this);
    const asset_dynamic_data_object& core_dd = core.dynamic_asset_data_id(*this);  

   _emission_parameters = _emission.get_parameters();

   _emission_parameters.emission_scale = get_global_properties().parameters.emission_scale * GRAPHENE_BLOCKCHAIN_PRECISION;
   _emission_parameters.delay_koefficient = get_global_properties().parameters.delay_koefficient;
   _emission_parameters.year_emission_limit = get_global_properties().parameters.year_emission_limit;

   _emission.set_parameters( _emission_parameters );
  
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
  _last_emission_processing_time = block_num;
  
   uint32_t current_activity = _activity_period.get_activity( );

   auto current_emission = _emission.calculate( core_dd.current_supply.value, _activity_period );
   _emission_state = _emission.get_emission_state();
   
   if( current_activity > _last_peak_activity )
      _last_peak_activity = current_activity;
    
   share_type distributed_current_emission(0);
  
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
   
    std::ofstream ifs;
    ifs.open( "emission.log", std::ofstream::app );
    if( ifs.is_open( ) )
      ifs << "--------------- emission started at " << Long2dt() << std::endl;
    ifs << "block_num = " << block_num << std::endl;
     
    ifs << "emission_period = " << get_global_properties().parameters.emission_period << std::endl;
    ifs << "emission_koefficient = " << get_global_properties().parameters.emission_koefficient << std::endl;
    ifs << "activity_weight = " << activity_weight << std::endl;
  
    ifs << "year_emission_limit = " << _emission_parameters.year_emission_limit << std::endl;
    ifs << "emission_scale = " << _emission_parameters.emission_scale << std::endl;
  
    ifs << "--end of parameters--" << std::endl;
     
    ifs << "current_activity = " << current_activity <<  std::endl;
    ifs << "last_peak_activity = " << std::to_string( _last_peak_activity ) << std::endl;
    ifs << "current_supply = " << std::to_string( core_dd.current_supply.value ) << std::endl;
    ifs << "current_emission = " << current_emission << std::endl;
  
    ifs << "name;balance;balance_share;activity;importance;emission" << std::endl;

    const auto& account_idx = get_index_type<account_index>().indices().get<by_name>();
    for( auto account = account_idx.begin(); account != account_idx.end(); account++ )
    {
        auto& balance_index = get_index_type<account_balance_index>().indices().get<by_account_asset>();
        auto itr = balance_index.find( boost::make_tuple( account->id, asset_id_type(0)) );
        if( itr != balance_index.end() )
        {
          singularity::gravity_index_calculator gic( activity_weight, core_dd.current_supply.value);
          double e = gic.calculate_index(itr->balance.value, account->activity_index) * current_emission;

          if( ifs.is_open( ) )
          {
            ifs << account->name << ";"
                << std::to_string( itr->balance.value ) << ";"
                << std::to_string( itr->balance.value / core_dd.current_supply.value ) << ";"
                << account->activity_index << ";"
                << std::to_string( itr->balance.value / core_dd.current_supply.value * ( 1 - activity_weight ) + account->activity_index * activity_weight ) << ";"
                << std::to_string( e ) << std::endl;
          }
           
          distributed_current_emission += e;
  
// TODO: keep general rating for every account in account property 
          adjust_balance( account->id, asset( e, asset_id_type(0) ) );    
          modify( *account, [&]( account_object& obj )
          {
             obj.emission_volume = e;
          });
        }
    }

    ifs << "--------------- emission ended at   " << Long2dt() << std::endl;
    if( ifs.is_open( ) )
      ifs.close( );

  
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
  
    modify( core_dd, [&]( asset_dynamic_data_object& obj )
    {
        obj.current_supply += distributed_current_emission;
    });

    modify( get_global_properties(), [&](global_property_object& p ) 
    {
        p.parameters.current_emission_volume = distributed_current_emission.value;
    });
  
    _activity_period.clear();
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
//***********************************************************************************************************************************************//    
  }
}
   
void database::process_poi( const uint32_t& block_num )
{
  std::cout << block_num << ":" << _last_activity_processing_time << "=" << block_num - _last_activity_processing_time << std::endl;

  std::ofstream ifs;
  ifs.open( "transaction.log", std::ofstream::app );
  if( ifs.is_open( ) )
  {
    ifs << "--------------- transactions started at " << Long2dt() << std::endl;
    ifs << "block_num = " << block_num << std::endl;
    ifs << "timestamp;from;to;amount;fee" << std::endl;
  }

  const asset_object& core = asset_id_type(0)(*this);
  std::vector<singularity::transaction_t> v;
  for( auto it = _pending_transactions.begin(); it != _pending_transactions.end(); it++ )
  {   
      const account_object& from_account = get( it->second.from );
      const account_object& to_account = get( it->second.to );
      const asset_object& asset_type = get( it->second.amount.asset_id );
  
      v.push_back({ asset_type.amount_to_real( it->second.amount.amount ), 
                      asset_type.amount_to_real( it->second.fee.amount ),
                        from_account.name, 
                          to_account.name,
                          std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() ), 
                          asset_type.amount_to_real( get_balance( from_account, core ).amount ),
                          asset_type.amount_to_real( get_balance( to_account, core ).amount ),
                          });  

      if( ifs.is_open( ) )
      {
            ifs << Long2dt() << ";"
                << get( it->second.from ).name << ";"
                << get( it->second.to ).name << ";"
                << asset_type.amount_to_real( it->second.amount.amount ) << ";"
                << asset_type.amount_to_real( it->second.fee.amount ) << std::endl;
      }                                                  
  }

  if( ifs.is_open( ) )
    ifs.close( );

  _activity_parameters = _ic.get_parameters();
  _activity_parameters.account_amount_threshold = get_global_properties().parameters.account_amount_threshold;
  _activity_parameters.transaction_amount_threshold = get_global_properties().parameters.transaction_amount_threshold;
  _activity_parameters.token_usd_rate = 0.1;
  _ic.set_parameters( _activity_parameters );  
  _ic.add_block( v );

  _activity_period.add_block( v );  
  _pending_transactions.clear();
  
  if( ( block_num - _last_activity_processing_time ) >= get_global_properties().parameters.activity_period )
  {
    std::ofstream ifs_calc;
    ifs_calc.open("activity.log", std::ofstream::app);
    if( ifs_calc.is_open( ) )
    {
      ifs_calc << "--------------- activity calculation started at " << Long2dt() << std::endl;
      ifs_calc << "block_num = " << block_num << std::endl;
    
      ifs_calc << "activity_period = " << get_global_properties().parameters.activity_period << std::endl;
      ifs_calc << "activity_weight = " << get_global_properties().parameters.activity_weight << std::endl;
      ifs_calc << "account_amount_threshold = " << _activity_parameters.account_amount_threshold << std::endl;
      ifs_calc << "transaction_amount_threshold = " << _activity_parameters.transaction_amount_threshold << std::endl;
      ifs_calc << "outlink_weight = " << _activity_parameters.outlink_weight << std::endl;
      ifs_calc << "interlevel_weight = " << _activity_parameters.interlevel_weight << std::endl;
      ifs_calc << "clustering_m = " << _activity_parameters.clustering_m << std::endl;
      ifs_calc << "clustering_e = " << _activity_parameters.clustering_e << std::endl;
      ifs_calc << "decay_period = " << _activity_parameters.decay_period << std::endl;
      ifs_calc << "decay_koefficient = " << _activity_parameters.decay_koefficient << std::endl;
      ifs_calc << "num_threads = " << _activity_parameters.num_threads << std::endl;
    
      ifs_calc << "--end of parameters--" << std::endl;
    }

    auto activity_index = _ic.calculate( );

    if( ifs_calc.is_open( ) )
      ifs_calc << "name;activity_index" << std::endl;

    const auto& idx = get_index_type<account_index>().indices().get<by_name>();
    for( auto it = activity_index.begin(); it != activity_index.end(); it++ )
    {
      auto itr = idx.find(it->first);
      if (itr != idx.end())
      {
        modify( *itr, [&it](account_object& b) 
        {
          b.activity_index = it->second;
        });  
        
        if( ifs_calc.is_open( ) )        
         ifs_calc << it->first << ";" << it->second << std::endl;
      }          
    }
    _last_activity_processing_time = block_num;

    ifs_calc << "--------------- activity calculation ended at   " << Long2dt() << std::endl;
    if( ifs_calc.is_open( ) )
      ifs_calc.close( );
  }
}
  
void database::clear_expired_orders()
{ try {
   detail::with_skip_flags( *this,
      get_node_properties().skip_flags | skip_authority_check, [&](){
         transaction_evaluation_state cancel_context(this);
  
         //Cancel expired limit orders
         auto& limit_index = get_index_type<limit_order_index>().indices().get<by_expiration>();
         while( !limit_index.empty() && limit_index.begin()->expiration <= head_block_time() )
         {
            limit_order_cancel_operation canceler;
            const limit_order_object& order = *limit_index.begin();
            canceler.fee_paying_account = order.seller;
            canceler.order = order.id;
            canceler.fee = current_fee_schedule().calculate_fee( canceler );
            if( canceler.fee.amount > order.deferred_fee )
            {
               // Cap auto-cancel fees at deferred_fee; see #549
               //wlog( "At block ${b}, fee for clearing expired order ${oid} was capped at deferred_fee ${fee}", ("b", head_block_num())("oid", order.id)("fee", order.deferred_fee) );
               canceler.fee = asset( order.deferred_fee, asset_id_type() );
            }
            // we know the fee for this op is set correctly since it is set by the chain.
            // this allows us to avoid a hung chain:
            // - if #549 case above triggers
            // - if the fee is incorrect, which may happen due to #435 (although since cancel is a fixed-fee op, it shouldn't)
            cancel_context.skip_fee_schedule_check = true;
            apply_operation(cancel_context, canceler);
         }
     });
  
   //Process expired force settlement orders
   auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();
   if( !settlement_index.empty() )
   {
      asset_id_type current_asset = settlement_index.begin()->settlement_asset_id();
      asset max_settlement_volume;
      bool extra_dump = false;
  
      auto next_asset = [&current_asset, &settlement_index, &extra_dump] {
         auto bound = settlement_index.upper_bound(current_asset);
         if( bound == settlement_index.end() )
         {
            if( extra_dump )
            {
               ilog( "next_asset() returning false" );
            }
            return false;
         }
         if( extra_dump )
         {
            ilog( "next_asset returning true, bound is ${b}", ("b", *bound) );
         }
         current_asset = bound->settlement_asset_id();
         return true;
      };
  
      uint32_t count = 0;
  
      // At each iteration, we either consume the current order and remove it, or we move to the next asset
      for( auto itr = settlement_index.lower_bound(current_asset);
           itr != settlement_index.end();
           itr = settlement_index.lower_bound(current_asset) )
      {
         ++count;
         const force_settlement_object& order = *itr;
         auto order_id = order.id;
         current_asset = order.settlement_asset_id();
         const asset_object& mia_object = get(current_asset);
         const asset_bitasset_data_object& mia = mia_object.bitasset_data(*this);
  
         extra_dump = ((count >= 1000) && (count <= 1020));
  
         if( extra_dump )
         {
            wlog( "clear_expired_orders() dumping extra data for iteration ${c}", ("c", count) );
            ilog( "head_block_num is ${hb} current_asset is ${a}", ("hb", head_block_num())("a", current_asset) );
         }
  
         if( mia.has_settlement() )
         {
            ilog( "Canceling a force settlement because of black swan" );
            cancel_order( order );
            continue;
         }
  
         // Has this order not reached its settlement date?
         if( order.settlement_date > head_block_time() )
         {
            if( next_asset() )
            {
               if( extra_dump )
               {
                  ilog( "next_asset() returned true when order.settlement_date > head_block_time()" );
               }
               continue;
            }
            break;
         }
         // Can we still settle in this asset?
         if( mia.current_feed.settlement_price.is_null() )
         {
            ilog("Canceling a force settlement in ${asset} because settlement price is null",
                 ("asset", mia_object.symbol));
            cancel_order(order);
            continue;
         }
         if( max_settlement_volume.asset_id != current_asset )
            max_settlement_volume = mia_object.amount(mia.max_force_settlement_volume(mia_object.dynamic_data(*this).current_supply));
         if( mia.force_settled_volume >= max_settlement_volume.amount )
         {
            /*
            ilog("Skipping force settlement in ${asset}; settled ${settled_volume} / ${max_volume}",
                 ("asset", mia_object.symbol)("settlement_price_null",mia.current_feed.settlement_price.is_null())
                 ("settled_volume", mia.force_settled_volume)("max_volume", max_settlement_volume));
                 */
            if( next_asset() )
            {
               if( extra_dump )
               {
                  ilog( "next_asset() returned true when mia.force_settled_volume >= max_settlement_volume.amount" );
               }
               continue;
            }
            break;
         }
  
         auto& pays = order.balance;
         auto receives = (order.balance * mia.current_feed.settlement_price);
         receives.amount = (fc::uint128_t(receives.amount.value) *
                            (GRAPHENE_100_PERCENT - mia.options.force_settlement_offset_percent) / GRAPHENE_100_PERCENT).to_uint64();
         assert(receives <= order.balance * mia.current_feed.settlement_price);
  
         price settlement_price = pays / receives;
  
         auto& call_index = get_index_type<call_order_index>().indices().get<by_collateral>();
         asset settled = mia_object.amount(mia.force_settled_volume);
         // Match against the least collateralized short until the settlement is finished or we reach max settlements
         while( settled < max_settlement_volume && find_object(order_id) )
         {
            auto itr = call_index.lower_bound(boost::make_tuple(price::min(mia_object.bitasset_data(*this).options.short_backing_asset,
                                                                           mia_object.get_id())));
            // There should always be a call order, since asset exists!
            assert(itr != call_index.end() && itr->debt_type() == mia_object.get_id());
            asset max_settlement = max_settlement_volume - settled;
  
            if( order.balance.amount == 0 )
            {
               wlog( "0 settlement detected" );
               cancel_order( order );
               break;
            }
            try {
               settled += match(*itr, order, settlement_price, max_settlement);
            } 
            catch ( const black_swan_exception& e ) { 
               wlog( "black swan detected: ${e}", ("e", e.to_detail_string() ) );
               cancel_order( order );
               break;
            }
         }
         if( mia.force_settled_volume != settled.amount )
         {
            modify(mia, [settled](asset_bitasset_data_object& b) {
               b.force_settled_volume = settled.amount;
            });
         }
      }
   }
} FC_CAPTURE_AND_RETHROW() }
  
void database::update_expired_feeds()
{
   auto& asset_idx = get_index_type<asset_index>().indices().get<by_type>();
   auto itr = asset_idx.lower_bound( true /** market issued */ );
   while( itr != asset_idx.end() )
   {
      const asset_object& a = *itr;
      ++itr;
      assert( a.is_market_issued() );
  
      const asset_bitasset_data_object& b = a.bitasset_data(*this);
      bool feed_is_expired;
      if( head_block_time() < HARDFORK_615_TIME )
         feed_is_expired = b.feed_is_expired_before_hardfork_615( head_block_time() );
      else
         feed_is_expired = b.feed_is_expired( head_block_time() );
      if( feed_is_expired )
      {
         modify(b, [this](asset_bitasset_data_object& a) {
            a.update_median_feeds(head_block_time());
         });
         check_call_orders(b.current_feed.settlement_price.base.asset_id(*this));
      }
      if( !b.current_feed.core_exchange_rate.is_null() &&
          a.options.core_exchange_rate != b.current_feed.core_exchange_rate )
         modify(a, [&b](asset_object& a) {
            a.options.core_exchange_rate = b.current_feed.core_exchange_rate;
         });
   }
}
  
void database::update_maintenance_flag( bool new_maintenance_flag )
{
   modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& dpo )
   {
      auto maintenance_flag = dynamic_global_property_object::maintenance_flag;
      dpo.dynamic_flags =
           (dpo.dynamic_flags & ~maintenance_flag)
         | (new_maintenance_flag ? maintenance_flag : 0);
   } );
   return;
}
  
void database::update_withdraw_permissions()
{
   auto& permit_index = get_index_type<withdraw_permission_index>().indices().get<by_expiration>();
   while( !permit_index.empty() && permit_index.begin()->expiration <= head_block_time() )
      remove(*permit_index.begin());
}
  
} }
