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

   struct activity_buffer_entity
   {
       uint32_t index;
       std::vector<uint8_t> data;
   };

   class gravity_activity_object : public graphene::db::abstract_object<gravity_activity_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = gravity_activity_object_type;

         int local_index;
         std::vector<activity_buffer_entity> activity_buffer;
   };

   struct by_local_index {};
   /**
    * @ingroup object_index
    */
   typedef multi_index_container
   <
      gravity_activity_object,
      indexed_by<
      ordered_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
      ordered_non_unique< tag<by_local_index>, member<gravity_activity_object, int, &gravity_activity_object::local_index> >
   >
   > gravity_activity_multi_index_type;

   /**
    * @ingroup object_index
    */
   typedef generic_index<gravity_activity_object, gravity_activity_multi_index_type> gravity_activity_index;
}}

FC_REFLECT( graphene::chain::activity_buffer_entity,(index)(data) )

FC_REFLECT_DERIVED( graphene::chain::gravity_activity_object,
                   ( graphene::db::object ),
                   ( local_index )
                   ( activity_buffer )
                  )
