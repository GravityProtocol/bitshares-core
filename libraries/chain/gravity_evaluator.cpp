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
#include <graphene/chain/gravity_evaluator.hpp>
#include <graphene/chain/gravity_transfer_object.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/uuid/string_generator.hpp>

namespace graphene { namespace chain {

//**********************************************************************************************************************************************//  
void_result gravity_transfer_evaluator::do_evaluate( const gravity_transfer_operation& op )
{ try {
   
   const database& d = db();

   const account_object& from_account    = op.from(d);
   const account_object& to_account      = op.to(d);
   const account_object& fee_payer       = op.fee_payer_account(d);
   const asset_object&   asset_type      = op.amount.asset_id(d);

   try {

      GRAPHENE_ASSERT(
         is_authorized_asset( d, from_account, asset_type ),
         transfer_from_account_not_whitelisted,
         "'from' account ${from} is not whitelisted for asset ${asset}",
         ("from",op.from)
         ("asset",op.amount.asset_id)
         );
      GRAPHENE_ASSERT(
         is_authorized_asset( d, to_account, asset_type ),
         transfer_to_account_not_whitelisted,
         "'to' account ${to} is not whitelisted for asset ${asset}",
         ("to",op.to)
         ("asset",op.amount.asset_id)
         );

      if( asset_type.is_transfer_restricted() )
      {
         GRAPHENE_ASSERT(
            from_account.id == asset_type.issuer || to_account.id == asset_type.issuer,
            transfer_restricted_transfer_asset,
            "Asset {asset} has transfer_restricted flag enabled",
            ("asset", op.amount.asset_id)
          );
      }

      FC_ASSERT( ( from_account.id != fee_payer.id || to_account.id != fee_payer.id ), "wrong fee_payer account" );

      bool insufficient_balance = d.get_balance( from_account, asset_type ).amount >= op.amount.amount;
      FC_ASSERT( insufficient_balance,
                 "Insufficient Balance: ${balance}, unable to transfer '${total_transfer}' from account '${a}' to '${t}'", 
                 ("a",from_account.name)("t",to_account.name)("total_transfer",d.to_pretty_string(op.amount))("balance",d.to_pretty_string(d.get_balance(from_account, asset_type))) );

      return void_result();
   } FC_RETHROW_EXCEPTIONS( error, "Unable to transfer ${a} from ${f} to ${t}", ("a",d.to_pretty_string(op.amount))("f",op.from(d).name)("t",op.to(d).name) );

}  FC_CAPTURE_AND_RETHROW( (op) ) }

void_result gravity_transfer_evaluator::do_apply( const gravity_transfer_operation& o )
{ try {
      const auto& gto = db( ).create<gravity_transfer_object>( [&]( gravity_transfer_object& obj )
      {
         std::string time_str = boost::posix_time::to_iso_string( boost::posix_time::from_time_t( db( ).head_block_time( ).sec_since_epoch( ) ) );
         std::string id = fc::to_string( obj.id.space( ) ) + "." + fc::to_string( obj.id.type( ) ) + "." + fc::to_string( obj.id.instance( ) );
         boost::uuids::string_generator gen;
         boost::uuids::uuid u1 = gen( id + time_str + time_str );
         obj.uuid = to_string( u1 );

         obj.fee = o.fee;
         obj.from = o.from;
         obj.to = o.to;
         obj.amount = o.amount;
         obj.fee_payer = o.fee_payer_account;
         obj.memo = o.memo;
      });
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }
//**********************************************************************************************************************************************//
void_result gravity_transfer_approve_evaluator::do_evaluate( const gravity_transfer_approve_operation& op )
{ 
   return void_result();
}  

void_result gravity_transfer_approve_evaluator::do_apply( const gravity_transfer_approve_operation& o )
{ try {

   bool gravity_transfer_founded = false;
   auto& e = db( ).get_index_type<gravity_transfer_index>( ).indices( ).get<by_transfer_by_uuid>( );

   for( auto itr = e.begin( ); itr != e.end( ); itr++ )
   {       
      if( ( *itr ).uuid.compare( o.uuid ) == 0 )
      {
            if( ( *itr ).to != o.approver )
                  FC_ASSERT( 0, "wrong receiver!" );

            db().adjust_balance( ( *itr ).from, -( *itr ).amount );
            db().adjust_balance( ( *itr ).to, ( *itr ).amount );

            auto c = db().get_core_asset();
            const auto& a = ( *itr ).amount.asset_id( (database&)db() ); 

            db( ).remove( *itr );
            gravity_transfer_founded = true;
            break;
      }
   }
   FC_ASSERT( gravity_transfer_founded, "gravity transfer not founed!" );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }
//**********************************************************************************************************************************************//
void_result gravity_transfer_reject_evaluator::do_evaluate( const gravity_transfer_reject_operation& op )
{ 
   return void_result();
}  

void_result gravity_transfer_reject_evaluator::do_apply( const gravity_transfer_reject_operation& o )
{ try {
   bool gravity_transfer_founded = false;
   auto& e = db( ).get_index_type<gravity_transfer_index>( ).indices( ).get<by_transfer_by_uuid>( );

   for( auto itr = e.begin( ); itr != e.end( ); itr++ )
   {       
      if( ( *itr ).uuid.compare( o.uuid ) == 0 )
      {
            if( ( *itr ).to != o.approver )
                  FC_ASSERT( 0, "wrong receiver!" );

            db( ).remove( *itr );
            gravity_transfer_founded = true;
            break;
      }
   }
   FC_ASSERT( gravity_transfer_founded, "gravity transfer not founed!" );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }
//**********************************************************************************************************************************************//
} } // graphene::chain
