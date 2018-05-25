#include <graphene/chain/gravity_emission_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain 
{
    void gravity_emission_object::process( graphene::chain::database* db, const double& emission_volume )
    {       
        if( emission_volume > _max_emission_volume )
        {
            emission_entity ee;
            ee.emission_volume = emission_volume - _max_emission_volume;
            _max_emission_volume = ee.emission_volume;

            auto& accounts = db->get_index_type<account_index>().indices().get<by_name>();
            double total_accounts = (double)accounts.size();
            double user_gain = ee.emission_volume / total_accounts;
          
            for(auto itr = accounts.begin(); itr != accounts.end(); itr++)
                ee.accounts[( *itr ).name] = user_gain;
            
            emission_history[db->head_block_time()] = ee;
        }       
    }
}}