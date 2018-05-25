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
#pragma once

#include <graphene/chain/protocol/operations.hpp>
#include <graphene/db/generic_index.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <fc/reflect/reflect.hpp>
#include <graphene/chain/protocol/config.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>

namespace graphene { namespace chain {
   class database;

   class gravity_transfer_object : public graphene::db::abstract_object<gravity_transfer_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = gravity_transfer_object_type;

         // Object uuid
         std::string uuid; 

         asset            fee;
         /// Account to transfer asset from
         account_id_type  from;
         /// Account to transfer asset to
         account_id_type  to;
         /// The amount of asset to transfer from @ref from to @ref to
         asset            amount;
         /// Fee payer
         account_id_type  fee_payer;

         /// User provided data encrypted to the memo key of the "to" account
         optional<memo_data> memo;
   };

   struct by_fee_payer{};
   struct by_sender{};
   struct by_receiver{};
   struct by_transfer_by_uuid{};

   /**
    * @ingroup object_index
    */
   typedef multi_index_container
   <
      gravity_transfer_object,
      indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_non_unique< tag<by_fee_payer>, member<gravity_transfer_object, account_id_type, &gravity_transfer_object::fee_payer> >,
      ordered_non_unique< tag<by_sender>, member<gravity_transfer_object, account_id_type, &gravity_transfer_object::from> >,
      ordered_non_unique< tag<by_receiver>, member<gravity_transfer_object, account_id_type, &gravity_transfer_object::to> >,
      ordered_non_unique< tag<by_transfer_by_uuid>, member<gravity_transfer_object, string, &gravity_transfer_object::uuid> >
   >
   > gravity_transfer_multi_index_type;

   /**
    * @ingroup object_index
    */
   typedef generic_index<gravity_transfer_object, gravity_transfer_multi_index_type> gravity_transfer_index;
}}

FC_REFLECT_DERIVED( graphene::chain::gravity_transfer_object,
                   ( graphene::db::object ), 
                   ( uuid )
                   ( fee )
                   ( from )
                   ( to )
                   ( amount )
                   ( fee_payer )
                   ( memo )
                  )
