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

#include <graphene/app/database_api.hpp>
#include <graphene/chain/get_config.hpp>

#include <graphene/chain/access_layer.hpp>
#include <graphene/chain/das33_evaluator.hpp>
#include <graphene/chain/withdrawal_limit_object.hpp>
#include <graphene/chain/issued_asset_record_object.hpp>

#include <fc/bloom_filter.hpp>

#include <fc/crypto/hex.hpp>
#include <fc/uint128.hpp>

#include <boost/range/iterator_range.hpp>
#include <boost/rational.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <cctype>
#include <cmath>

#include <cfenv>
#include <iostream>

#define GET_REQUIRED_FEES_MAX_RECURSION 4

namespace graphene { namespace app {

typedef std::map< std::pair<graphene::chain::asset_id_type, graphene::chain::asset_id_type>, std::vector<fc::variant> > market_queue_type;

class database_api_impl;


class database_api_impl : public std::enable_shared_from_this<database_api_impl>
{
   public:
      explicit database_api_impl( graphene::chain::database& db, const application_options* app_options );
      ~database_api_impl();

      // Objects
      fc::variants get_objects(const vector<object_id_type>& ids)const;

      // Subscriptions
      void set_subscribe_callback( std::function<void(const variant&)> cb, bool notify_remove_create );
      void set_pending_transaction_callback( std::function<void(const variant&)> cb );
      void set_block_applied_callback( std::function<void(const variant& block_id)> cb );
      void cancel_all_subscriptions();

      // Blocks and transactions
      optional<block_header> get_block_header(uint32_t block_num)const;
      map<uint32_t, optional<block_header>> get_block_header_batch(const vector<uint32_t> block_nums)const;
      optional<signed_block> get_block(uint32_t block_num)const;
      vector<signed_block_with_num> get_blocks(uint32_t block_num, uint32_t count) const;
      vector<signed_block_with_virtual_operations_and_num> get_blocks_with_virtual_operations(uint32_t start_block_num,
                                                                                              uint32_t count,
                                                                                              std::vector<uint16_t>& virtual_operation_ids) const;
      processed_transaction get_transaction( uint32_t block_num, uint32_t trx_in_block )const;

      // Globals
      chain_property_object get_chain_properties()const;
      global_property_object get_global_properties()const;
      fc::variant_object get_config()const;
      chain_id_type get_chain_id()const;
      dynamic_global_property_object get_dynamic_global_properties()const;
      optional<total_cycles_res> get_total_cycles() const;
      optional<queue_projection_res> get_queue_projection() const;

      // Keys
      vector<vector<account_id_type>> get_key_references( vector<public_key_type> key )const;

      // Accounts
      vector<optional<account_object>> get_accounts(const vector<account_id_type>& account_ids)const;
      std::map<string,full_account> get_full_accounts( const vector<string>& names_or_ids, bool subscribe );
      optional<account_object> get_account_by_name( string name )const;
      vector<account_id_type> get_account_references( account_id_type account_id )const;
      vector<optional<account_object>> lookup_account_names(const vector<string>& account_names)const;
      map<string,account_id_type> lookup_accounts(const string& lower_bound_name, uint32_t limit)const;
      uint64_t get_account_count()const;

      // Balances
      vector<asset_reserved> get_account_balances(account_id_type id, const flat_set<asset_id_type>& assets)const;
      vector<asset_reserved> get_named_account_balances(const std::string& name, const flat_set<asset_id_type>& assets)const;
      vector<balance_object> get_balance_objects( const vector<address>& addrs )const;
      vector<asset> get_vested_balances( const vector<balance_id_type>& objs )const;
      vector<vesting_balance_object> get_vesting_balances( account_id_type account_id )const;
      tethered_accounts_balances_collection get_tethered_accounts_balances( account_id_type id, asset_id_type asset )const;
      vector<tethered_accounts_balances_collection> get_tethered_accounts_balances( account_id_type account, const flat_set<asset_id_type>& assets )const;

      // Assets
      asset_id_type get_web_asset_id() const;
      vector<optional<asset_object>> get_assets(const vector<asset_id_type>& asset_ids)const;
      vector<asset_object>           list_assets(const string& lower_bound_symbol, uint32_t limit)const;
      vector<optional<asset_object>> lookup_asset_symbols(const vector<string>& symbols_or_ids)const;
      optional<asset_object> lookup_asset_symbol(const string& symbol_or_id) const;
      optional<issued_asset_record_object> get_issued_asset_record(const string& unique_id, asset_id_type asset_id) const;
      bool check_issued_asset(const string& unique_id, const string& asset) const;
      bool check_issued_webeur(const string& unique_id) const;

      // Markets / feeds
      vector<limit_order_object>         get_limit_orders(asset_id_type a, asset_id_type b, uint32_t limit)const;
      vector<limit_order_object>         get_limit_orders_for_account(account_id_type id, asset_id_type a, asset_id_type b, uint32_t limit)const;
      template<typename T>
      using repack_function = std::function<void(std::vector<T>&, std::map<share_type, aggregated_limit_orders_with_same_price> &, bool)>;
      template<typename T, typename Collection> T get_limit_orders_grouped_by_price(asset_id_type a, asset_id_type b, uint32_t limit, uint32_t precision, repack_function<Collection>)const;
      vector<call_order_object>          get_call_orders(asset_id_type a, uint32_t limit)const;
      vector<force_settlement_object>    get_settle_orders(asset_id_type a, uint32_t limit)const;
      vector<call_order_object>          get_margin_positions( const account_id_type& id )const;
      void subscribe_to_market(std::function<void(const variant&)> callback, asset_id_type a, asset_id_type b);
      void unsubscribe_from_market(asset_id_type a, asset_id_type b);
      market_ticker                      get_ticker( const string& base, const string& quote )const;
      market_hi_low_volume               get_24_hi_low_volume( const string& base, const string& quote )const;
      order_book                         get_order_book( const string& base, const string& quote, unsigned limit = 50 )const;
      vector<market_trade>               get_trade_history( const string& base, const string& quote, fc::time_point_sec start, fc::time_point_sec stop, unsigned limit = 100 )const;
      vector<market_trade>               get_trade_history_by_sequence( const string& base, const string& quote, int64_t start, fc::time_point_sec stop, unsigned limit = 100 )const;

      // Witnesses
      vector<optional<witness_object>> get_witnesses(const vector<witness_id_type>& witness_ids)const;
      fc::optional<witness_object> get_witness_by_account(account_id_type account)const;
      map<string, witness_id_type> lookup_witness_accounts(const string& lower_bound_name, uint32_t limit)const;
      uint64_t get_witness_count()const;

      // Committee members
      vector<optional<committee_member_object>> get_committee_members(const vector<committee_member_id_type>& committee_member_ids)const;
      fc::optional<committee_member_object> get_committee_member_by_account(account_id_type account)const;
      map<string, committee_member_id_type> lookup_committee_member_accounts(const string& lower_bound_name, uint32_t limit)const;

      // Authority / validation
      std::string get_transaction_hex(const signed_transaction& trx)const;
      set<public_key_type> get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const;
      set<public_key_type> get_potential_signatures( const signed_transaction& trx )const;
      set<address> get_potential_address_signatures( const signed_transaction& trx )const;
      bool verify_authority( const signed_transaction& trx )const;
      bool verify_account_authority( const string& name_or_id, const flat_set<public_key_type>& signers )const;
      processed_transaction validate_transaction( const signed_transaction& trx )const;
      vector< fc::variant > get_required_fees( const vector<operation>& ops, asset_id_type id )const;

      // Proposed transactions
      vector<proposal_object> get_proposed_transactions( account_id_type id )const;

      // Blinded balances
      vector<blinded_balance_object> get_blinded_balances( const flat_set<commitment_type>& commitments )const;

      // Licenses:
      optional<license_type_object> get_license_type(license_type_id_type license_id) const;
      vector<license_type_object> get_license_types() const;
      vector<optional<license_type_object>> get_license_types(const vector<license_type_id_type>& license_type_ids) const;
      vector<pair<string, license_type_id_type>> get_license_type_names_ids() const;
      vector<license_types_grouped_by_kind_res> get_license_type_names_ids_grouped_by_kind() const;
      vector<license_objects_grouped_by_kind_res> get_license_objects_grouped_by_kind() const;
      vector<optional<license_information_object>> get_license_information(const vector<account_id_type>& account_ids) const;
      vector<upgrade_event_object> get_upgrade_events() const;

      // Access:
      acc_id_share_t_res get_free_cycle_balance(account_id_type account_id) const;
      acc_id_vec_cycle_agreement_res get_all_cycle_balances(account_id_type account_id) const;
      acc_id_share_t_res get_dascoin_balance(account_id_type id) const;

      vector<acc_id_share_t_res> get_free_cycle_balances_for_accounts(vector<account_id_type> ids) const;
      vector<acc_id_vec_cycle_agreement_res> get_all_cycle_balances_for_accounts(vector<account_id_type> ids) const;
      vector<acc_id_share_t_res> get_dascoin_balances_for_accounts(vector<account_id_type> ids) const;

      vector<reward_queue_object> get_reward_queue() const;
      vector<reward_queue_object> get_reward_queue_by_page(uint32_t from, uint32_t amount) const;
      acc_id_queue_subs_w_pos_res get_queue_submissions_with_pos(account_id_type account_id) const;
      vector<acc_id_queue_subs_w_pos_res>
          get_queue_submissions_with_pos_for_accounts(vector<account_id_type> ids) const;
      uint32_t get_reward_queue_size() const;

      // Vault info:
      optional<vault_info_res> get_vault_info(account_id_type vault_id) const;
      vector<acc_id_vault_info_res> get_vaults_info(vector<account_id_type> vault_ids) const;

      optional<cycle_price> calculate_cycle_price(share_type cycle_amount, asset_id_type asset_id) const;

      vector<dasc_holder> get_top_dasc_holders() const;

      optional<withdrawal_limit> get_withdrawal_limit(account_id_type account, asset_id_type asset_id) const;

      // DasPay:
      vector<payment_service_provider_object> get_payment_service_providers() const;
      optional<vector<daspay_authority>> get_daspay_authority_for_account(account_id_type account) const;
      vector<delayed_operation_object> get_delayed_operations_for_account(account_id_type account) const;

      // Das33
      vector<das33_pledge_holder_object> get_das33_pledges(das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const;
      das33_pledges_by_account_result get_das33_pledges_by_account(account_id_type account) const;
      vector<das33_pledge_holder_object> get_das33_pledges_by_project(das33_project_id_type project, das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const;
      vector<das33_project_object> get_das33_projects(const string& lower_bound_name, uint32_t limit) const;
      vector<asset> get_amount_of_assets_pledged_to_project(das33_project_id_type project) const;
      vector<asset> get_amount_of_assets_pledged_to_project_in_phase(das33_project_id_type project, uint32_t phase) const;
      das33_project_tokens_amount get_amount_of_project_tokens_received_for_asset(das33_project_id_type project, asset to_pledge) const;
      das33_project_tokens_amount get_amount_of_asset_needed_for_project_token(das33_project_id_type project, asset_id_type asset_id, asset tokens) const;

      // Prices:
      vector<last_price_object> get_last_prices() const;
      vector<external_price_object> get_external_prices() const;

      template<typename T>
      void subscribe_to_item( const T& i )const
      {
         auto vec = fc::raw::pack(i);
         if( !_subscribe_callback )
            return;

         if( !is_subscribed_to_item(i) )
            _subscribe_filter.insert( vec.data(), vec.size() );
      }

      template<typename T>
      bool is_subscribed_to_item( const T& i )const
      {
         if( !_subscribe_callback )
            return false;

         return _subscribe_filter.contains( i );
      }

      bool is_impacted_account( const flat_set<account_id_type>& accounts)
      {
         if( !_subscribed_accounts.size() || !accounts.size() )
            return false;

         return std::any_of(accounts.begin(), accounts.end(), [this](const account_id_type& account) {
            return _subscribed_accounts.find(account) != _subscribed_accounts.end();
         });
      }

      // TODO: figure out some way to use copy.
      template<typename IndexType, typename IndexBy>
      vector<typename IndexType::object_type> list_objects( size_t limit ) const
      {
         const auto& idx = _db.get_index_type<IndexType>().indices().template get<IndexBy>();

         vector<typename IndexType::object_type> result;
         result.reserve(limit);

         auto itr = idx.begin();

         while(limit-- && itr != idx.end())
            result.emplace_back(*itr++);

         return result;
      }

      template<typename IndexType, typename IndexBy>
      vector<typename IndexType::object_type> list_bounded_objects_indexed_by_string( const string& lower_bound,
                                                                                     uint32_t limit )
      {
         vector<typename IndexType::object_type> result;
         result.reserve(limit);

         const auto& idx = _db.get_index_type<IndexType>().indices().template get<IndexBy>();

         auto itr = idx.lower_bound(lower_bound);

         if( lower_bound == "" )
            itr = idx.begin();

         while(limit-- && itr != idx.end())
            result.emplace_back(*itr++);

         return result;
      }

      // TODO: refactor into template methods.
      template<typename IndexType, typename IndexBy>
      vector<typename IndexType::object_type> list_bounded_objects_indexed_by_num( const uint32_t amount,
                                                                                   uint32_t limit )
      {
         vector<typename IndexType::object_type> result;
         result.reserve(limit);

         const auto& idx = _db.get_index_type<IndexType>().indices().template get<IndexBy>();

         auto itr = idx.lower_bound(amount);

         if( amount == 0 )
            itr = idx.begin();

         while(limit-- && itr != idx.end())
            result.emplace_back(*itr++);

         return result;
      }

      template<typename IdType, typename IndexType, typename IndexBy>
      vector<optional<typename IndexType::object_type> > lookup_string_or_id(const vector<string>& str_or_id) const
      {
         const auto& idx = _db.get_index_type<IndexType>().indices().template get<IndexBy>();
         vector<optional<typename IndexType::object_type> > result;
         result.reserve(str_or_id.size());
         std::transform(str_or_id.begin(), str_or_id.end(), std::back_inserter(result),
                        [this, &idx](const string& str_or_id) -> optional<typename IndexType::object_type> {
            if( !str_or_id.empty() && std::isdigit(str_or_id[0]) )
            {
               auto ptr = _db.find(variant(str_or_id).as<IdType>(1));
               return ptr == nullptr ? optional<typename IndexType::object_type>() : *ptr;
            }
            auto itr = idx.find(str_or_id);
            return itr == idx.end() ? optional<typename IndexType::object_type>() : *itr;
         });
         return result;
      }

      template<typename IndexType, typename IndexBy>
      vector<typename IndexType::object_type> list_all_objects() const
      {
         const auto& idx = _db.get_index_type<IndexType>().indices().template get<IndexBy>();
         auto itr = idx.begin();
         vector<typename IndexType::object_type> result;

         while( itr != idx.end() )
            result.emplace_back(*itr++);

         return result;
      }

      template<typename IdType, typename IndexType, typename IndexBy>
      vector<optional<typename IndexType::object_type>> fetch_optionals_from_ids(const vector<IdType>& ids) const
      {
         const auto& idx = _db.get_index_type<IndexType>().indices().template get<IndexBy>();
         vector<optional<typename IndexType::object_type> > result;
         result.reserve(ids.size());
         std::transform(ids.begin(), ids.end(), std::back_inserter(result),
                        [this, &idx](IdType id) -> optional<typename IndexType::object_type> {
            auto itr = idx.find(id);
            return itr == idx.end() ? optional<typename IndexType::object_type>() : *itr;
         });
         return result;
      }

      template<typename T>
      void enqueue_if_subscribed_to_market(const object* obj, market_queue_type& queue, bool full_object=true)
      {
         const T* order = dynamic_cast<const T*>(obj);
         FC_ASSERT( order != nullptr);

         auto market = order->get_market();

         auto sub = _market_subscriptions.find( market );
         if( sub != _market_subscriptions.end() ) {
            queue[market].emplace_back( full_object ? obj->to_variant() : fc::variant(obj->id, 1) );
         }
      }

      void broadcast_updates( const vector<variant>& updates );
      void broadcast_market_updates( const market_queue_type& queue);
      void handle_object_changed(bool force_notify, bool full_object, const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts, std::function<const object*(object_id_type id)> find_object);

      /** called every time a block is applied to report the objects that were changed */
      void on_objects_new(const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts);
      void on_objects_changed(const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts);
      void on_objects_removed(const vector<object_id_type>& ids, const vector<const object*>& objs, const flat_set<account_id_type>& impacted_accounts);
      void on_applied_block();

      bool _notify_remove_create = false;
      mutable fc::bloom_filter _subscribe_filter;
      std::set<account_id_type> _subscribed_accounts;
      std::function<void(const fc::variant&)> _subscribe_callback;
      std::function<void(const fc::variant&)> _pending_trx_callback;
      std::function<void(const fc::variant&)> _block_applied_callback;

      boost::signals2::scoped_connection _new_connection;
      boost::signals2::scoped_connection _change_connection;
      boost::signals2::scoped_connection _removed_connection;
      boost::signals2::scoped_connection _applied_block_connection;
      boost::signals2::scoped_connection _pending_trx_connection;
      map< pair<asset_id_type,asset_id_type>, std::function<void(const variant&)> > _market_subscriptions;
      graphene::chain::database& _db;
      database_access_layer _dal;
      const application_options* _app_options = nullptr;

      template<typename Iter>
      void func_re_pack(Iter helper_itr, Iter end, std::vector<aggregated_limit_orders_with_same_price_collection>& ret, uint32_t limit_group, uint32_t limit_per_group) const;
};

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Constructors                                                     //
//                                                                  //
//////////////////////////////////////////////////////////////////////

database_api::database_api( graphene::chain::database& db, const application_options* app_options )
   : my( new database_api_impl( db, app_options ) ) {}

database_api::~database_api() {}

database_api_impl::database_api_impl( graphene::chain::database& db, const application_options* app_options  )
: _db(db), _dal(db), _app_options(app_options)
{
   wlog("creating database api ${x}", ("x",int64_t(this)) );
   _new_connection = _db.new_objects.connect([this](const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts) {
                                             on_objects_new(ids, impacted_accounts);
                                            });
   _change_connection = _db.changed_objects.connect([this](const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts) {
                                on_objects_changed(ids, impacted_accounts);
                                });
   _removed_connection = _db.removed_objects.connect([this](const vector<object_id_type>& ids, const vector<const object*>& objs, const flat_set<account_id_type>& impacted_accounts) {
                                                     on_objects_removed(ids, objs, impacted_accounts);
                                                   });
   _applied_block_connection = _db.applied_block.connect([this](const signed_block&){ on_applied_block(); });

   _pending_trx_connection = _db.on_pending_transaction.connect([this](const signed_transaction& trx ){
                         if( _pending_trx_callback ) _pending_trx_callback( fc::variant(trx, GRAPHENE_MAX_NESTED_OBJECTS) );
                      });
}

database_api_impl::~database_api_impl()
{
   elog("freeing database api ${x}", ("x",int64_t(this)) );
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Objects                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

fc::variants database_api::get_objects(const vector<object_id_type>& ids)const
{
   return my->get_objects( ids );
}

fc::variants database_api_impl::get_objects(const vector<object_id_type>& ids)const
{
   if( _subscribe_callback )  {
      for( auto id : ids )
      {
         if( id.type() == operation_history_object_type && id.space() == protocol_ids ) continue;
         if( id.type() == impl_account_transaction_history_object_type && id.space() == implementation_ids ) continue;

         this->subscribe_to_item( id );
      }
   }
   else
   {
      elog( "getObjects without subscribe callback??" );
   }

   fc::variants result;
   result.reserve(ids.size());

   std::transform(ids.begin(), ids.end(), std::back_inserter(result),
                  [this](object_id_type id) -> fc::variant {
      if(auto obj = _db.find_object(id))
         return obj->to_variant();
      return {};
   });

   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Subscriptions                                                    //
//                                                                  //
//////////////////////////////////////////////////////////////////////

void database_api::set_subscribe_callback( std::function<void(const variant&)> cb, bool notify_remove_create )
{
   my->set_subscribe_callback( cb, notify_remove_create );
}

void database_api_impl::set_subscribe_callback( std::function<void(const variant&)> cb, bool notify_remove_create )
{
   if( notify_remove_create )
   {
      FC_ASSERT( _app_options && _app_options->enable_subscribe_to_all,
                 "Subscribing to universal object creation and removal is disallowed in this server." );
   }

   _subscribe_callback = cb;
   _notify_remove_create = notify_remove_create;
   _subscribed_accounts.clear();

   static fc::bloom_parameters param;
   param.projected_element_count    = 10000;
   param.false_positive_probability = 1.0/100;
   param.maximum_size = 1024*8*8*2;
   param.compute_optimal_parameters();
   _subscribe_filter = fc::bloom_filter(param);
}

void database_api::set_pending_transaction_callback( std::function<void(const variant&)> cb )
{
   my->set_pending_transaction_callback( cb );
}

void database_api_impl::set_pending_transaction_callback( std::function<void(const variant&)> cb )
{
   _pending_trx_callback = cb;
}

void database_api::set_block_applied_callback( std::function<void(const variant& block_id)> cb )
{
   my->set_block_applied_callback( cb );
}

void database_api_impl::set_block_applied_callback( std::function<void(const variant& block_id)> cb )
{
   _block_applied_callback = cb;
}

void database_api::cancel_all_subscriptions()
{
   my->cancel_all_subscriptions();
}

void database_api_impl::cancel_all_subscriptions()
{
   set_subscribe_callback( std::function<void(const fc::variant&)>(), true);
   _market_subscriptions.clear();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Blocks and transactions                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<block_header> database_api::get_block_header(uint32_t block_num)const
{
   return my->get_block_header( block_num );
}

optional<block_header> database_api_impl::get_block_header(uint32_t block_num) const
{
   auto result = _db.fetch_block_by_number(block_num);
   if(result)
      return *result;
   return {};
}

map<uint32_t, optional<block_header>> database_api::get_block_header_batch(const vector<uint32_t> block_nums)const
{
   return my->get_block_header_batch( block_nums );
}

map<uint32_t, optional<block_header>> database_api_impl::get_block_header_batch(const vector<uint32_t> block_nums) const
{
   map<uint32_t, optional<block_header>> results;
   for (const uint32_t block_num : block_nums)
   {
      results[block_num] = get_block_header(block_num);
   }
   return results;
}

optional<signed_block> database_api::get_block(uint32_t block_num)const
{
   return my->get_block( block_num );
}

optional<signed_block> database_api_impl::get_block(uint32_t block_num)const
{
   return _db.fetch_block_by_number(block_num);
}

vector<signed_block_with_num> database_api::get_blocks(uint32_t start_block_num, uint32_t count) const
{
    return my->get_blocks(start_block_num, count);
}

vector<signed_block_with_num> database_api_impl::get_blocks(uint32_t start_block_num, uint32_t count) const
{
    return _dal.get_blocks(start_block_num, count);
}

vector<signed_block_with_virtual_operations_and_num> database_api::get_blocks_with_virtual_operations(uint32_t start_block_num,
                                                                               uint32_t count,
                                                                               std::vector<uint16_t> virtual_operation_ids) const
{
    return my->get_blocks_with_virtual_operations(start_block_num, count, virtual_operation_ids);
}

vector<signed_block_with_virtual_operations_and_num> database_api_impl::get_blocks_with_virtual_operations(uint32_t start_block_num,
                                                                                    uint32_t count,
                                                                                    std::vector<uint16_t>& virtual_operation_ids) const
{
    return _dal.get_blocks_with_virtual_operations(start_block_num, count, virtual_operation_ids);
}

processed_transaction database_api::get_transaction( uint32_t block_num, uint32_t trx_in_block )const
{
   return my->get_transaction( block_num, trx_in_block );
}

optional<signed_transaction> database_api::get_recent_transaction_by_id( const transaction_id_type& id )const
{
   try {
      return my->_db.get_recent_transaction( id );
   } catch ( ... ) {
      return optional<signed_transaction>();
   }
}

processed_transaction database_api_impl::get_transaction(uint32_t block_num, uint32_t trx_num)const
{
   auto opt_block = _db.fetch_block_by_number(block_num);
   FC_ASSERT( opt_block );
   FC_ASSERT( opt_block->transactions.size() > trx_num );
   return opt_block->transactions[trx_num];
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Globals                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

chain_property_object database_api::get_chain_properties()const
{
   return my->get_chain_properties();
}

chain_property_object database_api_impl::get_chain_properties()const
{
   return _db.get(chain_property_id_type());
}

global_property_object database_api::get_global_properties()const
{
   return my->get_global_properties();
}

global_property_object database_api_impl::get_global_properties()const
{
  return _dal.get_global_properties();
}

fc::variant_object database_api::get_config()const
{
   return my->get_config();
}

fc::variant_object database_api_impl::get_config()const
{
   return graphene::chain::get_config();
}

chain_id_type database_api::get_chain_id()const
{
   return my->get_chain_id();
}

chain_id_type database_api_impl::get_chain_id()const
{
   return _db.get_chain_id();
}

dynamic_global_property_object database_api::get_dynamic_global_properties()const
{
   return my->get_dynamic_global_properties();
}

dynamic_global_property_object database_api_impl::get_dynamic_global_properties()const
{
   return _db.get(dynamic_global_property_id_type());
}

optional<total_cycles_res> database_api::get_total_cycles() const {
    return my->get_total_cycles();
}

optional<total_cycles_res> database_api_impl::get_total_cycles() const {
    total_cycles_res result;
    const auto& accounts = _db.get_index_type<account_index>().indices().get<by_id>();

    for (const account_object& acc : accounts)
    {
        if (acc.is_vault())
        {
            optional<total_cycles_res> vaultCycles = _dal.get_total_cycles(acc.get_id());
            if (vaultCycles.valid())
            {
                result.total_cycles += vaultCycles->total_cycles;
                result.total_dascoin += vaultCycles->total_dascoin;
            }
        }
    }
    return result;
}

optional<queue_projection_res> database_api::get_queue_projection() const {
    return my->get_queue_projection();
}

optional<queue_projection_res> database_api_impl::get_queue_projection() const {
    queue_projection_res result;
    const auto& accounts = _db.get_index_type<account_index>().indices().get<by_id>();

    for (const account_object& acc : accounts)
    {
        if (acc.is_vault())
        {
            optional<queue_projection_res> vaultQueue = _dal.get_queue_state_for_account(acc.get_id());
            if (vaultQueue.valid())
            {
                result = result + *vaultQueue;
            }
        }
    }
    return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Keys                                                             //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<vector<account_id_type>> database_api::get_key_references( vector<public_key_type> key )const
{
   return my->get_key_references( key );
}

/**
 *  @return all accounts that referr to the key or account id in their owner or active authorities.
 */
vector<vector<account_id_type>> database_api_impl::get_key_references( vector<public_key_type> keys )const
{
   wdump( (keys) );
   vector< vector<account_id_type> > final_result;
   final_result.reserve(keys.size());

   for( auto& key : keys )
   {

      address a1( pts_address(key, false, 56) );
      address a2( pts_address(key, true, 56) );
      address a3( pts_address(key, false, 0)  );
      address a4( pts_address(key, true, 0)  );
      address a5( key );

      subscribe_to_item( key );
      subscribe_to_item( a1 );
      subscribe_to_item( a2 );
      subscribe_to_item( a3 );
      subscribe_to_item( a4 );
      subscribe_to_item( a5 );

      const auto& idx = _db.get_index_type<account_index>();
      const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
      const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
      auto itr = refs.account_to_key_memberships.find(key);
      vector<account_id_type> result;

      for( auto& a : {a1,a2,a3,a4,a5} )
      {
          auto itr = refs.account_to_address_memberships.find(a);
          if( itr != refs.account_to_address_memberships.end() )
          {
             result.reserve( itr->second.size() );
             for( auto item : itr->second )
             {
                wdump((a)(item)(item(_db).name));
                result.push_back(item);
             }
          }
      }

      if( itr != refs.account_to_key_memberships.end() )
      {
         result.reserve( itr->second.size() );
         for( auto item : itr->second ) result.push_back(item);
      }
      final_result.emplace_back( std::move(result) );
   }

   for( auto i : final_result )
      subscribe_to_item(i);

   return final_result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Accounts                                                         //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<optional<account_object>> database_api::get_accounts(const vector<account_id_type>& account_ids)const
{
   return my->get_accounts( account_ids );
}

vector<optional<account_object>> database_api_impl::get_accounts(const vector<account_id_type>& account_ids)const
{
   vector<optional<account_object>> result; result.reserve(account_ids.size());
   std::transform(account_ids.begin(), account_ids.end(), std::back_inserter(result),
                  [this](account_id_type id) -> optional<account_object> {
      if(auto o = _db.find(id))
      {
         subscribe_to_item( id );
         return *o;
      }
      return {};
   });
   return result;
}

std::map<string,full_account> database_api::get_full_accounts( const vector<string>& names_or_ids, bool subscribe )
{
   return my->get_full_accounts( names_or_ids, subscribe );
}

std::map<std::string, full_account> database_api_impl::get_full_accounts( const vector<std::string>& names_or_ids, bool subscribe)
{
   idump((names_or_ids));
   std::map<std::string, full_account> results;

   for (const std::string& account_name_or_id : names_or_ids)
   {
      const account_object* account = nullptr;
      if (std::isdigit(account_name_or_id[0]))
         account = _db.find(fc::variant(account_name_or_id, 1).as<account_id_type>(1));
      else
      {
         const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
         auto itr = idx.find(account_name_or_id);
         if (itr != idx.end())
            account = &*itr;
      }
      if (account == nullptr)
         continue;

      if( subscribe )
      {
         if(_subscribed_accounts.size() < 100) {
            _subscribed_accounts.insert( account->get_id() );
            subscribe_to_item( account->id );
         }
      }

      // fc::mutable_variant_object full_account;
      full_account acnt;
      acnt.account = *account;
      acnt.statistics = account->statistics(_db);
      acnt.registrar_name = account->registrar(_db).name;
      acnt.referrer_name = account->referrer(_db).name;
      acnt.lifetime_referrer_name = account->lifetime_referrer(_db).name;
      acnt.votes.clear();

      // Add the account itself, its statistics object, cashback balance, and referral account names
      /*
      full_account("account", *account)("statistics", account->statistics(_db))
            ("registrar_name", account->registrar(_db).name)("referrer_name", account->referrer(_db).name)
            ("lifetime_referrer_name", account->lifetime_referrer(_db).name);
            */
      if (account->cashback_vb)
      {
         acnt.cashback_balance = account->cashback_balance(_db);
      }
      // Add the account's proposals
      const auto& proposal_idx = _db.get_index_type<proposal_index>();
      const auto& pidx = dynamic_cast<const primary_index<proposal_index>&>(proposal_idx);
      const auto& proposals_by_account = pidx.get_secondary_index<graphene::chain::required_approval_index>();
      auto  required_approvals_itr = proposals_by_account._account_to_proposals.find( account->id );
      if( required_approvals_itr != proposals_by_account._account_to_proposals.end() )
      {
         acnt.proposals.reserve( required_approvals_itr->second.size() );
         for( auto proposal_id : required_approvals_itr->second )
            acnt.proposals.push_back( proposal_id(_db) );
      }


      // Add the account's balances
      auto balance_range = _db.get_index_type<account_balance_index>().indices().get<by_account_asset>().equal_range(boost::make_tuple(account->id));
      //vector<account_balance_object> balances;
      std::for_each(balance_range.first, balance_range.second,
                    [&acnt](const account_balance_object& balance) {
                       acnt.balances.emplace_back(balance);
                    });

      // Add the account's vesting balances
      auto vesting_range = _db.get_index_type<vesting_balance_index>().indices().get<by_account>().equal_range(account->id);
      std::for_each(vesting_range.first, vesting_range.second,
                    [&acnt](const vesting_balance_object& balance) {
                       acnt.vesting_balances.emplace_back(balance);
                    });

      // Add the account's orders
      auto order_range = _db.get_index_type<limit_order_index>().indices().get<by_account>().equal_range(account->id);
      std::for_each(order_range.first, order_range.second,
                    [&acnt] (const limit_order_object& order) {
                       acnt.limit_orders.emplace_back(order);
                    });
      auto call_range = _db.get_index_type<call_order_index>().indices().get<by_account>().equal_range(account->id);
      std::for_each(call_range.first, call_range.second,
                    [&acnt] (const call_order_object& call) {
                       acnt.call_orders.emplace_back(call);
                    });
      results[account_name_or_id] = acnt;
   }
   return results;
}

optional<account_object> database_api::get_account_by_name( string name )const
{
   return my->get_account_by_name( name );
}

optional<account_object> database_api_impl::get_account_by_name( string name )const
{
   const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
   auto itr = idx.find(name);
   if (itr != idx.end())
      return *itr;
   return optional<account_object>();
}

vector<account_id_type> database_api::get_account_references( account_id_type account_id )const
{
   return my->get_account_references( account_id );
}

vector<account_id_type> database_api_impl::get_account_references( account_id_type account_id )const
{
   const auto& idx = _db.get_index_type<account_index>();
   const auto& aidx = dynamic_cast<const primary_index<account_index>&>(idx);
   const auto& refs = aidx.get_secondary_index<graphene::chain::account_member_index>();
   auto itr = refs.account_to_account_memberships.find(account_id);
   vector<account_id_type> result;

   if( itr != refs.account_to_account_memberships.end() )
   {
      result.reserve( itr->second.size() );
      for( auto item : itr->second ) result.push_back(item);
   }
   return result;
}

vector<optional<account_object>> database_api::lookup_account_names(const vector<string>& account_names)const
{
   return my->lookup_account_names( account_names );
}

vector<optional<account_object>> database_api_impl::lookup_account_names(const vector<string>& account_names)const
{
   const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
   vector<optional<account_object> > result;
   result.reserve(account_names.size());
   std::transform(account_names.begin(), account_names.end(), std::back_inserter(result),
                  [&accounts_by_name](const string& name) -> optional<account_object> {
      auto itr = accounts_by_name.find(name);
      return itr == accounts_by_name.end()? optional<account_object>() : *itr;
   });
   return result;
}

map<string,account_id_type> database_api::lookup_accounts(const string& lower_bound_name, uint32_t limit)const
{
   return my->lookup_accounts( lower_bound_name, limit );
}

map<string,account_id_type> database_api_impl::lookup_accounts(const string& lower_bound_name, uint32_t limit)const
{
   FC_ASSERT( limit <= 1000 );
   const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
   map<string,account_id_type> result;

   for( auto itr = accounts_by_name.lower_bound(lower_bound_name);
        limit-- && itr != accounts_by_name.end();
        ++itr )
   {
      result.insert(make_pair(itr->name, itr->get_id()));
      if( limit == 1 )
         subscribe_to_item( itr->get_id() );
   }

   return result;
}

uint64_t database_api::get_account_count()const
{
   return my->get_account_count();
}

uint64_t database_api_impl::get_account_count()const
{
   return _db.get_index_type<account_index>().indices().size();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Balances                                                         //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<asset_reserved> database_api::get_account_balances(account_id_type id, const flat_set<asset_id_type>& assets) const
{
   return my->get_account_balances( id, assets );
}

vector<asset_reserved> database_api_impl::get_account_balances(account_id_type acnt, const flat_set<asset_id_type>& assets) const
{
   vector<asset_reserved> result;
   if (assets.empty())
   {
      // if the caller passes in an empty list of assets, return balances for all assets the account owns.
      const account_balance_index& balance_index = _db.get_index_type<account_balance_index>();
      auto range = balance_index.indices().get<by_account_asset>().equal_range(boost::make_tuple(acnt));
      for ( const account_balance_object& balance : boost::make_iterator_range(range.first, range.second) )
         result.emplace_back(balance.get_asset_reserved_balance());
   }
   else
   {
      result.reserve(assets.size());
      std::transform(assets.begin(), assets.end(), std::back_inserter(result), [this, acnt](asset_id_type id) {
         return _db.get_balance_object(acnt, id).get_asset_reserved_balance();
      });
   }
   return result;
}

vector<asset_reserved> database_api::get_named_account_balances(const std::string& name, const flat_set<asset_id_type>& assets)const
{
   return my->get_named_account_balances( name, assets );
}

vector<asset_reserved> database_api_impl::get_named_account_balances(const std::string& name, const flat_set<asset_id_type>& assets) const
{
   const auto& accounts_by_name = _db.get_index_type<account_index>().indices().get<by_name>();
   auto itr = accounts_by_name.find(name);
   FC_ASSERT( itr != accounts_by_name.end() );
   return get_account_balances(itr->get_id(), assets);
}

vector<balance_object> database_api::get_balance_objects( const vector<address>& addrs )const
{
   return my->get_balance_objects( addrs );
}

vector<balance_object> database_api_impl::get_balance_objects( const vector<address>& addrs )const
{
   try
   {
      const auto& bal_idx = _db.get_index_type<balance_index>();
      const auto& by_owner_idx = bal_idx.indices().get<by_owner>();

      vector<balance_object> result;

      for( const auto& owner : addrs )
      {
         subscribe_to_item( owner );
         auto itr = by_owner_idx.lower_bound( boost::make_tuple( owner, asset_id_type(0) ) );
         while( itr != by_owner_idx.end() && itr->owner == owner )
         {
            result.push_back( *itr );
            ++itr;
         }
      }
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (addrs) )
}

vector<asset> database_api::get_vested_balances( const vector<balance_id_type>& objs )const
{
   return my->get_vested_balances( objs );
}

vector<asset> database_api_impl::get_vested_balances( const vector<balance_id_type>& objs )const
{
   try
   {
      vector<asset> result;
      result.reserve( objs.size() );
      auto now = _db.head_block_time();
      for( auto obj : objs )
         result.push_back( obj(_db).available( now ) );
      return result;
   } FC_CAPTURE_AND_RETHROW( (objs) )
}

vector<vesting_balance_object> database_api::get_vesting_balances( account_id_type account_id )const
{
   return my->get_vesting_balances( account_id );
}

vector<vesting_balance_object> database_api_impl::get_vesting_balances( account_id_type account_id )const
{
   try
   {
      vector<vesting_balance_object> result;
      auto vesting_range = _db.get_index_type<vesting_balance_index>().indices().get<by_account>().equal_range(account_id);
      std::for_each(vesting_range.first, vesting_range.second,
                    [&result](const vesting_balance_object& balance) {
                       result.emplace_back(balance);
                    });
      return result;
   }
   FC_CAPTURE_AND_RETHROW( (account_id) );
}

vector<tethered_accounts_balances_collection> database_api::get_tethered_accounts_balances( account_id_type id, const flat_set<asset_id_type>& assets )const
{
   return my->get_tethered_accounts_balances( id, assets );
}

vector<tethered_accounts_balances_collection> database_api_impl::get_tethered_accounts_balances( account_id_type account, const flat_set<asset_id_type>& assets )const
{
   vector<asset_id_type> tmp;
   if (assets.empty()) {
      // if the caller passes in an empty list of assets, get all assets the account owns.
      const account_balance_index &balance_index = _db.get_index_type<account_balance_index>();
      auto range = balance_index.indices().get<by_account_asset>().equal_range(boost::make_tuple(account));
      for (const account_balance_object &balance : boost::make_iterator_range(range.first, range.second))
         tmp.emplace_back(balance.asset_type);
   } else {
      tmp.reserve(assets.size());
      std::copy(assets.begin(), assets.end(), std::back_inserter(tmp));
   }
   vector<tethered_accounts_balances_collection> result;
   std::transform(tmp.begin(), tmp.end(), std::back_inserter(result), [this, account](asset_id_type id) {
      return get_tethered_accounts_balances(account, id);
   });
   return result;
}

tethered_accounts_balances_collection database_api_impl::get_tethered_accounts_balances( account_id_type id, asset_id_type asset )const
{
   tethered_accounts_balances_collection ret;
   ret.total = 0;
   ret.asset_id = asset;
   const auto& idx = _db.get_index_type<account_index>().indices().get<by_id>();
   const auto it = idx.find(id);
   flat_set<tuple<account_id_type, string, account_kind>> accounts;
   if (it != idx.end())
   {
      const auto& account = *it;
      if (account.kind == account_kind::wallet)
      {
         accounts.insert(make_tuple(id, account.name, account.kind));
         std::transform(account.vault.begin(), account.vault.end(), std::inserter(accounts, accounts.begin()), [&](account_id_type vault)
         {
            const auto& vault_acc = vault(_db);
            return make_tuple(vault, vault_acc.name, account_kind::vault);
         });
      }
      else if (account.kind == account_kind::custodian || account.kind == account_kind::special)
      {
         accounts.insert(make_tuple(id, account.name, account.kind));
      }
      else if (account.kind == account_kind::vault)
      {
          if (account.parents.empty())
             accounts.insert(make_tuple(id, account.name, account.kind));
          else
             return get_tethered_accounts_balances(*(account.parents.begin()), asset);
      }
   }

   for (const auto& i : accounts)
   {
      if (_db.check_if_balance_object_exists(get<0>(i), asset))
      {
         const auto& balance_obj = _db.get_balance_object(get<0>(i), asset);
         ret.total += balance_obj.balance + balance_obj.reserved;
         ret.details.emplace_back(tethered_accounts_balance{get<0>(i), get<1>(i), get<2>(i), balance_obj.balance, balance_obj.reserved});
      }
   }
   return ret;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Assets                                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////

asset_id_type database_api_impl::get_web_asset_id() const
{
    return _db.get_web_asset_id();
}

vector<optional<asset_object>> database_api::get_assets(const vector<asset_id_type>& asset_ids)const
{
   return my->get_assets( asset_ids );
}

vector<optional<asset_object>> database_api_impl::get_assets(const vector<asset_id_type>& asset_ids)const
{
   vector<optional<asset_object>> result; result.reserve(asset_ids.size());
   std::transform(asset_ids.begin(), asset_ids.end(), std::back_inserter(result),
                  [this](asset_id_type id) -> optional<asset_object> {
      if(auto o = _db.find(id))
      {
         subscribe_to_item( id );
         return *o;
      }
      return {};
   });
   return result;
}

vector<asset_object> database_api::list_assets(const string& lower_bound_symbol, uint32_t limit)const
{
   return my->list_assets( lower_bound_symbol, limit );
}

vector<asset_object> database_api_impl::list_assets(const string& lower_bound_symbol, uint32_t limit)const
{
   FC_ASSERT( limit <= 100 );
   const auto& assets_by_symbol = _db.get_index_type<asset_index>().indices().get<by_symbol>();
   vector<asset_object> result;
   result.reserve(limit);

   auto itr = assets_by_symbol.lower_bound(lower_bound_symbol);

   if( lower_bound_symbol == "" )
      itr = assets_by_symbol.begin();

   while(limit-- && itr != assets_by_symbol.end())
      result.emplace_back(*itr++);

   return result;
}

optional<asset_object> database_api::lookup_asset_symbol(const string& symbol_or_id) const
{
   return my->lookup_asset_symbol( symbol_or_id );
}

optional<asset_object> database_api_impl::lookup_asset_symbol(const string& symbol_or_id) const
{
   return _dal.lookup_asset_symbol(symbol_or_id);
}

vector<optional<asset_object>> database_api::lookup_asset_symbols(const vector<string>& symbols_or_ids)const
{
   return my->lookup_asset_symbols( symbols_or_ids );
}

vector<optional<asset_object>> database_api_impl::lookup_asset_symbols(const vector<string>& symbols_or_ids)const
{
   return _dal.lookup_asset_symbols( symbols_or_ids );
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Markets / feeds                                                  //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<limit_order_object> database_api::get_limit_orders(asset_id_type a, asset_id_type b, uint32_t limit)const
{
   return my->get_limit_orders( a, b, limit );
}

/**
 *  @return the limit orders for both sides of the book for the two assets specified up to limit number on each side.
 */
vector<limit_order_object> database_api_impl::get_limit_orders(asset_id_type a, asset_id_type b, uint32_t limit)const
{
   const auto& limit_order_idx = _db.get_index_type<limit_order_index>();
   const auto& limit_price_idx = limit_order_idx.indices().get<by_price>();

   vector<limit_order_object> result;

   uint32_t count = 0;
   auto limit_itr = limit_price_idx.lower_bound(price::max(a,b));
   auto limit_end = limit_price_idx.upper_bound(price::min(a,b));
   while(limit_itr != limit_end && count < limit)
   {
      result.push_back(*limit_itr);
      ++limit_itr;
      ++count;
   }
   count = 0;
   limit_itr = limit_price_idx.lower_bound(price::max(b,a));
   limit_end = limit_price_idx.upper_bound(price::min(b,a));
   while(limit_itr != limit_end && count < limit)
   {
      result.push_back(*limit_itr);
      ++limit_itr;
      ++count;
   }

   return result;
}

vector<limit_order_object> database_api::get_limit_orders_for_account(account_id_type id, asset_id_type a, asset_id_type b, uint32_t limit)const
{
   return my->get_limit_orders_for_account( id, a, b, limit );
}

/**
 *  @return the limit orders for a given account, for both sides of the book for the two assets specified up to limit number on each side.
 */
vector<limit_order_object> database_api_impl::get_limit_orders_for_account(account_id_type id, asset_id_type a, asset_id_type b, uint32_t limit)const
{
   FC_ASSERT( limit < 200, "Limit (${limit}) needs to be lower than 200", ("limit", limit) );
   const auto& limit_order_idx = _db.get_index_type<limit_order_index>();
   const auto& limit_account_idx = limit_order_idx.indices().get<by_account>();

   vector<limit_order_object> result;

   uint32_t count = 0;
   auto market = std::make_pair( a, b );
   if( market.first > market.second )
      std::swap( market.first, market.second );
   auto limit_itr = limit_account_idx.lower_bound(id);
   auto limit_end = limit_account_idx.upper_bound(id);
   while(limit_itr != limit_end && count < limit)
   {
      if (limit_itr->get_market() == market)
      {
         result.push_back(*limit_itr);
         ++count;
      }
      ++limit_itr;
   }

   return result;
}

limit_orders_grouped_by_price database_api::get_limit_orders_grouped_by_price(asset_id_type a, asset_id_type b, uint32_t limit)const
{
   return my->get_limit_orders_grouped_by_price<limit_orders_grouped_by_price, aggregated_limit_orders_with_same_price>( a, b, limit, ORDER_BOOK_QUERY_PRECISION, std::bind(&database_api::repack<aggregated_limit_orders_with_same_price>, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, limit) );
}

limit_orders_grouped_by_price database_api::get_limit_orders_grouped_by_price_with_precision(asset_id_type a, asset_id_type b, uint32_t limit, uint32_t precision)const
{
   return my->get_limit_orders_grouped_by_price<limit_orders_grouped_by_price, aggregated_limit_orders_with_same_price>( a, b, limit, asset::scaled_precision(precision).value, std::bind(&database_api::repack<aggregated_limit_orders_with_same_price>, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, limit) );
}

template<typename T>
void database_api::repack(std::vector<T>& ret, std::map<share_type, aggregated_limit_orders_with_same_price> &helper_map, bool ascending, uint32_t limit)const
{
   uint32_t count = 0;
   if(ascending)
   {
      auto helper_itr = helper_map.begin();
      while(helper_itr != helper_map.end() && count < limit)
      {
         ret.push_back(helper_itr->second);
         helper_itr++;
         count++;
      }
   }
   else
   {
      auto helper_itr = helper_map.rbegin();
      while(helper_itr != helper_map.rend() && count < limit)
      {
         ret.push_back(helper_itr->second);
         helper_itr++;
         count++;
      }
   }
}

template<typename T, typename Collection> T database_api_impl::get_limit_orders_grouped_by_price(asset_id_type base, asset_id_type quote, uint32_t limit, uint32_t precision, repack_function<Collection> repack)const
{
   const auto& limit_order_idx = _db.get_index_type<limit_order_index>();
   const auto& limit_price_idx = limit_order_idx.indices().get<by_price>();

   T result;
   bool swap_buy_sell = false;
   if(base < quote)
   {
      std::swap(base, quote);
      swap_buy_sell = true;
   }

   auto func = [this, &limit_price_idx, limit, precision, repack](asset_id_type& a, asset_id_type& b, std::vector<Collection>& ret, bool ascending){
      std::map<share_type, aggregated_limit_orders_with_same_price> helper_map;

      auto limit_itr = limit_price_idx.lower_bound(price::max(a, b));
      auto limit_end = limit_price_idx.upper_bound(price::min(a, b));

      auto& asset_a = _db.get(a);
      auto& asset_b = _db.get(b);
      double coef = asset::scaled_precision(asset_a.precision).value * 1.0 / asset::scaled_precision(asset_b.precision).value;

      while(limit_itr != limit_end)
      {
         double price = ascending ? 1 / limit_itr->sell_price.to_real() : limit_itr->sell_price.to_real();
         // adjust price precision and value accordingly so we can form a key
         auto p = round((ascending ? price * coef : price / coef) * precision);
         share_type price_key = static_cast<share_type>(p);

         auto helper_itr = helper_map.find(price_key);
         auto quote = round(ascending ? limit_itr->for_sale.value * price : limit_itr->for_sale.value / price);

         // if we are adding limit order with new price
         if(helper_itr == helper_map.end())
         {
            aggregated_limit_orders_with_same_price alo;
            alo.price = price_key;
            alo.base_volume = limit_itr->for_sale.value;
            alo.quote_volume = quote;
            alo.count = 1;

            helper_map[price_key] = alo;
         }
         else
         {
            helper_itr->second.base_volume += limit_itr->for_sale.value;
            helper_itr->second.quote_volume += quote;
            helper_itr->second.count++;
         }

         ++limit_itr;
      }

      // re-pack result in vector (from map) in desired order
      repack(ret, helper_map, ascending);
   };

   if(swap_buy_sell)
   {
      func(base, quote, result.buy, false);
      func(quote, base, result.sell, true);
   } else
   {
      func(base, quote, result.sell, true);
      func(quote, base, result.buy, false);
   }

   return result;
}

limit_orders_collection_grouped_by_price database_api::get_limit_orders_collection_grouped_by_price(asset_id_type a, asset_id_type b, uint32_t limit_group, uint32_t limit_per_group) const
{
   auto f = [this, limit_group, limit_per_group](std::vector<aggregated_limit_orders_with_same_price_collection>& ret, std::map<share_type, aggregated_limit_orders_with_same_price> &helper_map, bool ascending) {
      // re-pack result in vector (from map) in desired order
      if(ascending)
      {
         my->func_re_pack(helper_map.begin(), helper_map.end(), ret, limit_group, limit_per_group);
      }
      else
      {
         my->func_re_pack(helper_map.rbegin(), helper_map.rend(), ret, limit_group, limit_per_group);
      }
   };
   return my->get_limit_orders_grouped_by_price<limit_orders_collection_grouped_by_price, aggregated_limit_orders_with_same_price_collection>( a, b, 0, ORDER_BOOK_QUERY_PRECISION, f );
}

template<typename Iter>
void database_api_impl::func_re_pack(Iter helper_itr, Iter end, std::vector<aggregated_limit_orders_with_same_price_collection>& ret, uint32_t limit_group, uint32_t limit_per_group) const
{
   uint32_t count = 0;
   while(helper_itr != end && count < limit_group)
   {
      auto& alo = helper_itr->second;
      share_type group_price_key = static_cast<share_type>(alo.price / ORDER_BOOK_GROUP_QUERY_PRECISION_DIFF);
      aggregated_limit_orders_with_same_price_collection aloc;
      aloc.price = group_price_key;
      aloc.base_volume = alo.base_volume;
      aloc.quote_volume = alo.quote_volume;
      aloc.count = alo.count;
      aloc.limit_orders.push_back(alo);

      ret.push_back(aloc);
      helper_itr++;
      count++;
      // put all groups in same basket if price for group is same
      while(helper_itr != end)
      {
         alo = helper_itr->second;
         share_type group_price_key_temp = static_cast<share_type>(alo.price / ORDER_BOOK_GROUP_QUERY_PRECISION_DIFF);
         if(group_price_key_temp != group_price_key)
         {
            break;
         }
         if(aloc.limit_orders.size() < limit_per_group)
         {
            ret.back().count += alo.count;
            ret.back().base_volume += alo.base_volume;
            ret.back().quote_volume += alo.quote_volume;
            ret.back().limit_orders.push_back(alo);
         }
         helper_itr++;
      }
   }
}

vector<call_order_object> database_api::get_call_orders(asset_id_type a, uint32_t limit)const
{
   return my->get_call_orders( a, limit );
}

vector<call_order_object> database_api_impl::get_call_orders(asset_id_type a, uint32_t limit)const
{
   const auto& call_index = _db.get_index_type<call_order_index>().indices().get<by_price>();
   const asset_object& mia = _db.get(a);
   price index_price = price::min(mia.bitasset_data(_db).options.short_backing_asset, mia.get_id());
   
   vector< call_order_object> result;
   auto itr_min = call_index.lower_bound(index_price.min());
   auto itr_max = call_index.lower_bound(index_price.max());
   while( itr_min != itr_max && result.size() < limit ) 
   {
      result.emplace_back(*itr_min);
      ++itr_min;
   }
   return result;
}

vector<force_settlement_object> database_api::get_settle_orders(asset_id_type a, uint32_t limit)const
{
   return my->get_settle_orders( a, limit );
}

vector<force_settlement_object> database_api_impl::get_settle_orders(asset_id_type a, uint32_t limit)const
{
   const auto& settle_index = _db.get_index_type<force_settlement_index>().indices().get<by_expiration>();
   const asset_object& mia = _db.get(a);

   vector<force_settlement_object> result;
   auto itr_min = settle_index.lower_bound(mia.get_id());
   auto itr_max = settle_index.upper_bound(mia.get_id());
   while( itr_min != itr_max && result.size() < limit )
   {
      result.emplace_back(*itr_min);
      ++itr_min;
   }
   return result;
}

vector<call_order_object> database_api::get_margin_positions( const account_id_type& id )const
{
   return my->get_margin_positions( id );
}

vector<call_order_object> database_api_impl::get_margin_positions( const account_id_type& id )const
{
   try
   {
      const auto& idx = _db.get_index_type<call_order_index>();
      const auto& aidx = idx.indices().get<by_account>();
      auto start = aidx.lower_bound( boost::make_tuple( id, asset_id_type(0) ) );
      auto end = aidx.lower_bound( boost::make_tuple( id+1, asset_id_type(0) ) );
      vector<call_order_object> result;
      while( start != end )
      {
         result.push_back(*start);
         ++start;
      }
      return result;
   } FC_CAPTURE_AND_RETHROW( (id) )
}

void database_api::subscribe_to_market(std::function<void(const variant&)> callback, asset_id_type a, asset_id_type b)
{
   my->subscribe_to_market( callback, a, b );
}

void database_api_impl::subscribe_to_market(std::function<void(const variant&)> callback, asset_id_type a, asset_id_type b)
{
   if(a > b) std::swap(a,b);
   FC_ASSERT(a != b);
   _market_subscriptions[ std::make_pair(a,b) ] = callback;
}

void database_api::unsubscribe_from_market(asset_id_type a, asset_id_type b)
{
   my->unsubscribe_from_market( a, b );
}

void database_api_impl::unsubscribe_from_market(asset_id_type a, asset_id_type b)
{
   if(a > b) std::swap(a,b);
   FC_ASSERT(a != b);
   _market_subscriptions.erase(std::make_pair(a,b));
}

market_ticker database_api::get_ticker( const string& base, const string& quote )const
{
   return my->get_ticker( base, quote );
}

market_ticker database_api_impl::get_ticker( const string& base, const string& quote )const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   const auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   const fc::time_point_sec now = _db.head_block_time();
   const fc::time_point_sec yesterday = fc::time_point_sec( now.sec_since_epoch() - 86400 );

   market_ticker result;
   result.time = now;
   result.base = base;
   result.quote = quote;
   result.latest = 0;
   result.lowest_ask = 0;
   result.highest_bid = 0;
   result.percent_change = 0;
   result.base_volume = 0;
   result.quote_volume = 0;

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;
   if( base_id > quote_id ) std::swap( base_id, quote_id );

   history_key hkey;
   hkey.base = base_id;
   hkey.quote = quote_id;
   hkey.sequence = std::numeric_limits<int64_t>::min();

   // TODO: move following duplicate code out
   // TODO: using pow is a bit inefficient here, optimization is possible
   auto asset_to_real = [&]( const asset& a, int p ) { return double(a.amount.value)/pow( 10, p ); };
   auto price_to_real = [&]( const price& p )
   {
     if( p.base.asset_id == assets[0]->id )
        return asset_to_real( p.base, assets[0]->precision ) / asset_to_real( p.quote, assets[1]->precision );
     else
        return asset_to_real( p.quote, assets[0]->precision ) / asset_to_real( p.base, assets[1]->precision );
  };

   const auto& history_idx = _db.get_index_type<graphene::market_history::history_index>().indices().get<by_key>();
   auto itr = history_idx.lower_bound( hkey );

   bool is_latest = true;
   price latest_price;
   fc::uint128 base_volume;
   fc::uint128 quote_volume;
   while( itr != history_idx.end() && itr->key.base == base_id && itr->key.quote == quote_id )
   {
     if( is_latest )
     {
        is_latest = false;
        latest_price = itr->op.fill_price;
        result.latest = price_to_real( latest_price );
     }

     if( itr->time < yesterday )
     {
        if( itr->op.fill_price != latest_price )
          result.percent_change = ( result.latest / price_to_real( itr->op.fill_price ) - 1 ) * 100;
        break;
     }

     if( itr->op.is_maker )
     {
        if( assets[0]->id == itr->op.receives.asset_id )
        {
           base_volume += itr->op.receives.amount.value;
           quote_volume += itr->op.pays.amount.value;
        }
        else
        {
           base_volume += itr->op.pays.amount.value;
           quote_volume += itr->op.receives.amount.value;
        }
     }

     ++itr;
   }

   auto uint128_to_double = []( const fc::uint128& n )
   {
      if( n.hi == 0 ) return double( n.lo );
         return double(n.hi) * (uint64_t(1)<<63) * 2 + n.lo;
   };
   result.base_volume = uint128_to_double( base_volume ) / pow( 10, assets[0]->precision );
   result.quote_volume = uint128_to_double( quote_volume ) / pow( 10, assets[1]->precision );

   const auto orders = get_order_book( base, quote, 1 );
   if( !orders.asks.empty() ) result.lowest_ask = orders.asks[0].price;
   if( !orders.bids.empty() ) result.highest_bid = orders.bids[0].price;

   return result;
}

market_hi_low_volume database_api::get_24_hi_low_volume( const string& base, const string& quote )const
{
   return my->get_24_hi_low_volume( base, quote );
}

market_hi_low_volume database_api_impl::get_24_hi_low_volume( const string& base, const string& quote )const
{
   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;

   market_hi_low_volume result;
   result.base = base;
   result.quote = quote;
   result.high = 0;
   result.low = 0;
   result.base_volume = 0;
   result.quote_volume = 0;

   try {
      if( base_id > quote_id ) std::swap(base_id, quote_id);

      auto now = fc::time_point_sec( fc::time_point::now() );
      auto ts = now - fc::days(1).to_seconds();

      auto trades = get_trade_history( base, quote, now, ts, 100 );

      if( !trades.empty() )
      {
         result.high = trades[0].price;
         result.low = trades[0].price;
      }

      for ( market_trade t: trades )
      {
         if( result.high < t.price )
            result.high = t.price;
         if( result.low > t.price )
            result.low = t.price;

         result.base_volume += t.value;
         result.quote_volume += t.amount;
      }

      while (trades.size() == 100)
      {
         trades = get_trade_history_by_sequence( base, quote, trades[99].sequence, ts, 100 );

         for ( market_trade t: trades )
         {
           if( result.high < t.price )
              result.high = t.price;
           if( result.low > t.price )
              result.low = t.price;

           result.base_volume += t.value;
           result.quote_volume += t.amount;
         }
      }

      return result;
   } FC_CAPTURE_AND_RETHROW( (base)(quote) )
}

optional<issued_asset_record_object>
database_api_impl::get_issued_asset_record(const string& unique_id, asset_id_type asset_id) const
{
    return _dal.get_issued_asset_record(unique_id, asset_id);
}

bool database_api::check_issued_asset(const string& unique_id, const string& asset) const
{
    return my->check_issued_asset(unique_id, asset);
}

bool database_api_impl::check_issued_asset(const string& unique_id, const string& asset) const
{
    return _dal.check_issued_asset(unique_id, asset);
}

bool database_api::check_issued_webeur(const string& unique_id) const
{
    return my->check_issued_webeur(unique_id);
}

bool database_api_impl::check_issued_webeur(const string& unique_id) const
{
    return _dal.check_issued_webeur(unique_id);
}

order_book database_api::get_order_book( const string& base, const string& quote, unsigned limit )const
{
   return my->get_order_book( base, quote, limit);
}

order_book database_api_impl::get_order_book( const string& base, const string& quote, unsigned limit )const
{
   using boost::multiprecision::uint128_t;
   FC_ASSERT( limit <= 50 );

   order_book result;
   result.base = base;
   result.quote = quote;

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;
   auto orders = get_limit_orders( base_id, quote_id, limit );


   auto asset_to_real = [&]( const asset& a, int p ) { return double(a.amount.value)/pow( 10, p ); };
   auto price_to_real = [&]( const price& p )
   {
      if( p.base.asset_id == base_id )
         return asset_to_real( p.base, assets[0]->precision ) / asset_to_real( p.quote, assets[1]->precision );
      else
         return asset_to_real( p.quote, assets[0]->precision ) / asset_to_real( p.base, assets[1]->precision );
   };

   for( const auto& o : orders )
   {
      if( o.sell_price.base.asset_id == base_id )
      {
         order ord;
         ord.price = price_to_real( o.sell_price );
         ord.quote = asset_to_real( share_type( ( uint128_t( o.for_sale.value ) * o.sell_price.quote.amount.value ) / o.sell_price.base.amount.value ), assets[1]->precision );
         ord.base = asset_to_real( o.for_sale, assets[0]->precision );
         result.bids.push_back( ord );
      }
      else
      {
         order ord;
         ord.price = price_to_real( o.sell_price );
         ord.quote = asset_to_real( o.for_sale, assets[1]->precision );
         ord.base = asset_to_real( share_type( ( uint64_t( o.for_sale.value ) * o.sell_price.quote.amount.value ) / o.sell_price.base.amount.value ), assets[0]->precision );
         result.asks.push_back( ord );
      }
   }

   return result;
}

vector<market_trade> database_api::get_trade_history( const string& base,
                                                      const string& quote,
                                                      fc::time_point_sec start,
                                                      fc::time_point_sec stop,
                                                      unsigned limit )const
{
   return my->get_trade_history( base, quote, start, stop, limit );
}

vector<market_trade> database_api_impl::get_trade_history( const string& base,
                                                           const string& quote,
                                                           fc::time_point_sec start,
                                                           fc::time_point_sec stop,
                                                           unsigned limit )const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   FC_ASSERT( limit <= 100 );

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;

   if( base_id > quote_id ) std::swap( base_id, quote_id );

   auto asset_to_real = [&]( const asset& a, int p ) { return double( a.amount.value ) / pow( 10, p ); };
   auto price_to_real = [&]( const price& p )
   {
      if( p.base.asset_id == assets[0]->id )
         return asset_to_real( p.base, assets[0]->precision ) / asset_to_real( p.quote, assets[1]->precision );
      else
         return asset_to_real( p.quote, assets[0]->precision ) / asset_to_real( p.base, assets[1]->precision );
   };

   if ( start.sec_since_epoch() == 0 )
      start = fc::time_point_sec( fc::time_point::now() );

   uint32_t count = 0;
   const auto& history_idx = _db.get_index_type<graphene::market_history::history_index>().indices().get<by_market_time>();
   auto itr = history_idx.lower_bound( std::make_tuple( base_id, quote_id, start ) );
   vector<market_trade> result;

   while( itr != history_idx.end() && count < limit && !( itr->key.base != base_id || itr->key.quote != quote_id || itr->time < stop ) )
   {
      {
         market_trade trade;

         if( assets[0]->id == itr->op.receives.asset_id )
         {
            trade.amount = asset_to_real( itr->op.pays, assets[1]->precision );
            trade.value = asset_to_real( itr->op.receives, assets[0]->precision );
         }
         else
         {
            trade.amount = asset_to_real( itr->op.receives, assets[1]->precision );
            trade.value = asset_to_real( itr->op.pays, assets[0]->precision );
         }

         trade.date = itr->time;
         trade.price = price_to_real( itr->op.fill_price );

         if( itr->op.is_maker )
         {
            trade.sequence = -itr->key.sequence;
            trade.side1_account_id = itr->op.account_id;
         }
         else
            trade.side2_account_id = itr->op.account_id;

         auto next_itr = std::next(itr);
         // Trades are usually tracked in each direction, exception: for global settlement only one side is recorded
         if( next_itr != history_idx.end() && next_itr->key.base == base_id && next_itr->key.quote == quote_id
             && next_itr->time == itr->time && next_itr->op.is_maker != itr->op.is_maker )
         {  // next_itr now could be the other direction // FIXME not 100% sure
            if( next_itr->op.is_maker )
            {
               trade.sequence = -next_itr->key.sequence;
               trade.side1_account_id = next_itr->op.account_id;
            }
            else
               trade.side2_account_id = next_itr->op.account_id;
            // skip the other direction
            itr = next_itr;
         }

         result.push_back( trade );
         ++count;
      }

      ++itr;
   }

   return result;
}

vector<market_trade> database_api::get_trade_history_by_sequence(
                                                      const string& base,
                                                      const string& quote,
                                                      int64_t start,
                                                      fc::time_point_sec stop,
                                                      unsigned limit )const
{
   return my->get_trade_history_by_sequence( base, quote, start, stop, limit );
}

vector<market_trade> database_api_impl::get_trade_history_by_sequence(
                                                           const string& base,
                                                           const string& quote,
                                                           int64_t start,
                                                           fc::time_point_sec stop,
                                                           unsigned limit )const
{
   FC_ASSERT( _app_options && _app_options->has_market_history_plugin, "Market history plugin is not enabled." );

   FC_ASSERT( limit <= 100 );
   FC_ASSERT( start >= 0 );
   int64_t start_seq = -start;

   auto assets = lookup_asset_symbols( {base, quote} );
   FC_ASSERT( assets[0], "Invalid base asset symbol: ${s}", ("s",base) );
   FC_ASSERT( assets[1], "Invalid quote asset symbol: ${s}", ("s",quote) );

   auto base_id = assets[0]->id;
   auto quote_id = assets[1]->id;

   if( base_id > quote_id ) std::swap( base_id, quote_id );
   const auto& history_idx = _db.get_index_type<graphene::market_history::history_index>().indices().get<by_key>();
   history_key hkey;
   hkey.base = base_id;
   hkey.quote = quote_id;
   hkey.sequence = start_seq;

   auto asset_to_real = [&]( const asset& a, int p ) { return double( a.amount.value ) / pow( 10, p ); };
   auto price_to_real = [&]( const price& p )
   {
      if( p.base.asset_id == assets[0]->id )
         return asset_to_real( p.base, assets[0]->precision ) / asset_to_real( p.quote, assets[1]->precision );
      else
         return asset_to_real( p.quote, assets[0]->precision ) / asset_to_real( p.base, assets[1]->precision );
   };

   uint32_t count = 0;
   auto itr = history_idx.lower_bound( hkey );
   vector<market_trade> result;

   while( itr != history_idx.end() && count < limit && !( itr->key.base != base_id || itr->key.quote != quote_id || itr->time < stop ) )
   {
      if( itr->key.sequence == start_seq ) // found the key, should skip this and the other direction if found
      {
         auto next_itr = std::next(itr);
         if( next_itr != history_idx.end() && next_itr->key.base == base_id && next_itr->key.quote == quote_id
             && next_itr->time == itr->time && next_itr->op.is_maker != itr->op.is_maker )
         {  // next_itr now could be the other direction // FIXME not 100% sure
            // skip the other direction
            itr = next_itr;
         }
      }
      else
      {
         market_trade trade;

         if( assets[0]->id == itr->op.receives.asset_id )
         {
            trade.amount = asset_to_real( itr->op.pays, assets[1]->precision );
            trade.value = asset_to_real( itr->op.receives, assets[0]->precision );
         }
         else
         {
            trade.amount = asset_to_real( itr->op.receives, assets[1]->precision );
            trade.value = asset_to_real( itr->op.pays, assets[0]->precision );
         }

         trade.date = itr->time;
         trade.price = price_to_real( itr->op.fill_price );

         if( itr->op.is_maker )
         {
            trade.sequence = -itr->key.sequence;
            trade.side1_account_id = itr->op.account_id;
         }
         else
            trade.side2_account_id = itr->op.account_id;

         auto next_itr = std::next(itr);
         // Trades are usually tracked in each direction, exception: for global settlement only one side is recorded
         if( next_itr != history_idx.end() && next_itr->key.base == base_id && next_itr->key.quote == quote_id
             && next_itr->time == itr->time && next_itr->op.is_maker != itr->op.is_maker )
         {  // next_itr now could be the other direction // FIXME not 100% sure
            if( next_itr->op.is_maker )
            {
               trade.sequence = -next_itr->key.sequence;
               trade.side1_account_id = next_itr->op.account_id;
            }
            else
               trade.side2_account_id = next_itr->op.account_id;
            // skip the other direction
            itr = next_itr;
         }

         result.push_back( trade );
         ++count;
      }

      ++itr;
   }

   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Witnesses                                                        //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<optional<witness_object>> database_api::get_witnesses(const vector<witness_id_type>& witness_ids)const
{
   return my->get_witnesses( witness_ids );
}

vector<worker_object> database_api::get_workers_by_account(account_id_type account)const
{
    const auto& idx = my->_db.get_index_type<worker_index>().indices().get<by_account>();
    auto itr = idx.find(account);
    vector<worker_object> result;

    if( itr != idx.end() && itr->worker_account == account )
    {
       result.emplace_back( *itr );
       ++itr;
    }

    return result;
}


vector<optional<witness_object>> database_api_impl::get_witnesses(const vector<witness_id_type>& witness_ids)const
{
   vector<optional<witness_object>> result; result.reserve(witness_ids.size());
   std::transform(witness_ids.begin(), witness_ids.end(), std::back_inserter(result),
                  [this](witness_id_type id) -> optional<witness_object> {
      if(auto o = _db.find(id))
         return *o;
      return {};
   });
   return result;
}

fc::optional<witness_object> database_api::get_witness_by_account(account_id_type account)const
{
   return my->get_witness_by_account( account );
}

fc::optional<witness_object> database_api_impl::get_witness_by_account(account_id_type account) const
{
   const auto& idx = _db.get_index_type<witness_index>().indices().get<by_account>();
   auto itr = idx.find(account);
   if( itr != idx.end() )
      return *itr;
   return {};
}

map<string, witness_id_type> database_api::lookup_witness_accounts(const string& lower_bound_name, uint32_t limit)const
{
   return my->lookup_witness_accounts( lower_bound_name, limit );
}

map<string, witness_id_type> database_api_impl::lookup_witness_accounts(const string& lower_bound_name, uint32_t limit)const
{
   FC_ASSERT( limit <= 1000 );
   const auto& witnesses_by_id = _db.get_index_type<witness_index>().indices().get<by_id>();

   // we want to order witnesses by account name, but that name is in the account object
   // so the witness_index doesn't have a quick way to access it.
   // get all the names and look them all up, sort them, then figure out what
   // records to return.  This could be optimized, but we expect the
   // number of witnesses to be few and the frequency of calls to be rare
   std::map<std::string, witness_id_type> witnesses_by_account_name;
   for (const witness_object& witness : witnesses_by_id)
       if (auto account_iter = _db.find(witness.witness_account))
           if (account_iter->name >= lower_bound_name) // we can ignore anything below lower_bound_name
               witnesses_by_account_name.insert(std::make_pair(account_iter->name, witness.id));

   auto end_iter = witnesses_by_account_name.begin();
   while (end_iter != witnesses_by_account_name.end() && limit--)
       ++end_iter;
   witnesses_by_account_name.erase(end_iter, witnesses_by_account_name.end());
   return witnesses_by_account_name;
}

uint64_t database_api::get_witness_count()const
{
   return my->get_witness_count();
}

uint64_t database_api_impl::get_witness_count()const
{
   return _db.get_index_type<witness_index>().indices().size();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Committee members                                                //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<optional<committee_member_object>> database_api::get_committee_members(const vector<committee_member_id_type>& committee_member_ids)const
{
   return my->get_committee_members( committee_member_ids );
}

vector<optional<committee_member_object>> database_api_impl::get_committee_members(const vector<committee_member_id_type>& committee_member_ids)const
{
   vector<optional<committee_member_object>> result; result.reserve(committee_member_ids.size());
   std::transform(committee_member_ids.begin(), committee_member_ids.end(), std::back_inserter(result),
                  [this](committee_member_id_type id) -> optional<committee_member_object> {
      if(auto o = _db.find(id))
         return *o;
      return {};
   });
   return result;
}

fc::optional<committee_member_object> database_api::get_committee_member_by_account(account_id_type account)const
{
   return my->get_committee_member_by_account( account );
}

fc::optional<committee_member_object> database_api_impl::get_committee_member_by_account(account_id_type account) const
{
   const auto& idx = _db.get_index_type<committee_member_index>().indices().get<by_account>();
   auto itr = idx.find(account);
   if( itr != idx.end() )
      return *itr;
   return {};
}

map<string, committee_member_id_type> database_api::lookup_committee_member_accounts(const string& lower_bound_name, uint32_t limit)const
{
   return my->lookup_committee_member_accounts( lower_bound_name, limit );
}

map<string, committee_member_id_type> database_api_impl::lookup_committee_member_accounts(const string& lower_bound_name, uint32_t limit)const
{
   FC_ASSERT( limit <= 1000 );
   const auto& committee_members_by_id = _db.get_index_type<committee_member_index>().indices().get<by_id>();

   // we want to order committee_members by account name, but that name is in the account object
   // so the committee_member_index doesn't have a quick way to access it.
   // get all the names and look them all up, sort them, then figure out what
   // records to return.  This could be optimized, but we expect the
   // number of committee_members to be few and the frequency of calls to be rare
   std::map<std::string, committee_member_id_type> committee_members_by_account_name;
   for (const committee_member_object& committee_member : committee_members_by_id)
       if (auto account_iter = _db.find(committee_member.committee_member_account))
           if (account_iter->name >= lower_bound_name) // we can ignore anything below lower_bound_name
               committee_members_by_account_name.insert(std::make_pair(account_iter->name, committee_member.id));

   auto end_iter = committee_members_by_account_name.begin();
   while (end_iter != committee_members_by_account_name.end() && limit--)
       ++end_iter;
   committee_members_by_account_name.erase(end_iter, committee_members_by_account_name.end());
   return committee_members_by_account_name;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Authority / validation                                           //
//                                                                  //
//////////////////////////////////////////////////////////////////////

std::string database_api::get_transaction_hex(const signed_transaction& trx)const
{
   return my->get_transaction_hex( trx );
}

std::string database_api_impl::get_transaction_hex(const signed_transaction& trx)const
{
   return fc::to_hex(fc::raw::pack(trx));
}

set<public_key_type> database_api::get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const
{
   return my->get_required_signatures( trx, available_keys );
}

set<public_key_type> database_api_impl::get_required_signatures( const signed_transaction& trx, const flat_set<public_key_type>& available_keys )const
{
   wdump((trx)(available_keys));
   auto result = trx.get_required_signatures( _db.get_chain_id(),
                                       available_keys,
                                       [&]( account_id_type id ){ return &id(_db).active; },
                                       [&]( account_id_type id ){ return &id(_db).owner; },
                                       _db.get_global_properties().parameters.max_authority_depth );
   wdump((result));
   return result;
}

set<public_key_type> database_api::get_potential_signatures( const signed_transaction& trx )const
{
   return my->get_potential_signatures( trx );
}
set<address> database_api::get_potential_address_signatures( const signed_transaction& trx )const
{
   return my->get_potential_address_signatures( trx );
}

set<public_key_type> database_api_impl::get_potential_signatures( const signed_transaction& trx )const
{
   wdump((trx));
   set<public_key_type> result;
   trx.get_required_signatures(
      _db.get_chain_id(),
      flat_set<public_key_type>(),
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).active;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return &auth;
      },
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).owner;
         for( const auto& k : auth.get_keys() )
            result.insert(k);
         return &auth;
      },
      _db.get_global_properties().parameters.max_authority_depth
   );

   wdump((result));
   return result;
}

set<address> database_api_impl::get_potential_address_signatures( const signed_transaction& trx )const
{
   set<address> result;
   trx.get_required_signatures(
      _db.get_chain_id(),
      flat_set<public_key_type>(),
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).active;
         for( const auto& k : auth.get_addresses() )
            result.insert(k);
         return &auth;
      },
      [&]( account_id_type id )
      {
         const auto& auth = id(_db).owner;
         for( const auto& k : auth.get_addresses() )
            result.insert(k);
         return &auth;
      },
      _db.get_global_properties().parameters.max_authority_depth
   );
   return result;
}

bool database_api::verify_authority( const signed_transaction& trx )const
{
   return my->verify_authority( trx );
}

bool database_api_impl::verify_authority( const signed_transaction& trx )const
{
   trx.verify_authority( _db.get_chain_id(),
                         [&]( account_id_type id ){ return &id(_db).active; },
                         [&]( account_id_type id ){ return &id(_db).owner; },
                          _db.get_global_properties().parameters.max_authority_depth );
   return true;
}

bool database_api::verify_account_authority( const string& name_or_id, const flat_set<public_key_type>& signers )const
{
   return my->verify_account_authority( name_or_id, signers );
}

bool database_api_impl::verify_account_authority( const string& name_or_id, const flat_set<public_key_type>& keys )const
{
   FC_ASSERT( name_or_id.size() > 0);
   const account_object* account = nullptr;
   if (std::isdigit(name_or_id[0]))
      account = _db.find(fc::variant(name_or_id, 1).as<account_id_type>(1));
   else
   {
      const auto& idx = _db.get_index_type<account_index>().indices().get<by_name>();
      auto itr = idx.find(name_or_id);
      if (itr != idx.end())
         account = &*itr;
   }
   FC_ASSERT( account, "no such account" );


   /// reuse trx.verify_authority by creating a dummy transfer
   signed_transaction trx;
   transfer_operation op;
   op.from = account->id;
   trx.operations.emplace_back(op);

   return verify_authority( trx );
}

processed_transaction database_api::validate_transaction( const signed_transaction& trx )const
{
   return my->validate_transaction( trx );
}

processed_transaction database_api_impl::validate_transaction( const signed_transaction& trx )const
{
   return _db.validate_transaction(trx);
}

vector< fc::variant > database_api::get_required_fees( const vector<operation>& ops, asset_id_type id )const
{
   return my->get_required_fees( ops, id );
}

/**
 * Container method for mutually recursive functions used to
 * implement get_required_fees() with potentially nested proposals.
 */
struct get_required_fees_helper
{
   get_required_fees_helper(
      const fee_schedule& _current_fee_schedule,
      const price& _core_exchange_rate,
      uint32_t _max_recursion
      )
      : current_fee_schedule(_current_fee_schedule),
        core_exchange_rate(_core_exchange_rate),
        max_recursion(_max_recursion)
   {}

   fc::variant set_op_fees( operation& op )
   {
      if( op.which() == operation::tag<proposal_create_operation>::value )
      {
         return set_proposal_create_op_fees( op );
      }
      else
      {
         asset fee = current_fee_schedule.set_fee( op, core_exchange_rate );
         fc::variant result;
         fc::to_variant( fee, result, GRAPHENE_MAX_NESTED_OBJECTS );
         return result;
      }
   }

   fc::variant set_proposal_create_op_fees( operation& proposal_create_op )
   {
      proposal_create_operation& op = proposal_create_op.get<proposal_create_operation>();
      std::pair< asset, fc::variants > result;
      for( op_wrapper& prop_op : op.proposed_ops )
      {
         FC_ASSERT( current_recursion < max_recursion );
         ++current_recursion;
         result.second.push_back( set_op_fees( prop_op.op ) );
         --current_recursion;
      }
      // we need to do this on the boxed version, which is why we use
      // two mutually recursive functions instead of a visitor
      result.first = current_fee_schedule.set_fee( proposal_create_op, core_exchange_rate );
      fc::variant vresult;
      fc::to_variant( result, vresult, GRAPHENE_MAX_NESTED_OBJECTS );
      return vresult;
   }

   const fee_schedule& current_fee_schedule;
   const price& core_exchange_rate;
   uint32_t max_recursion;
   uint32_t current_recursion = 0;
};

vector< fc::variant > database_api_impl::get_required_fees( const vector<operation>& ops, asset_id_type id )const
{
   vector< operation > _ops = ops;
   //
   // we copy the ops because we need to mutate an operation to reliably
   // determine its fee, see #435
   //

   vector< fc::variant > result;
   result.reserve(ops.size());
   const asset_object& a = id(_db);
   get_required_fees_helper helper(
      _db.current_fee_schedule(),
      a.options.core_exchange_rate,
      GET_REQUIRED_FEES_MAX_RECURSION );
   for( operation& op : _ops )
   {
      result.push_back( helper.set_op_fees( op ) );
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Proposed transactions                                            //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<proposal_object> database_api::get_proposed_transactions( account_id_type id )const
{
   return my->get_proposed_transactions( id );
}

/** TODO: add secondary index that will accelerate this process */
vector<proposal_object> database_api_impl::get_proposed_transactions( account_id_type id )const
{
   const auto& idx = _db.get_index_type<proposal_index>();
   vector<proposal_object> result;

   idx.inspect_all_objects( [&](const object& obj){
           const proposal_object& p = static_cast<const proposal_object&>(obj);
           if( p.required_active_approvals.find( id ) != p.required_active_approvals.end() )
              result.push_back(p);
           else if ( p.required_owner_approvals.find( id ) != p.required_owner_approvals.end() )
              result.push_back(p);
           else if ( p.available_active_approvals.find( id ) != p.available_active_approvals.end() )
              result.push_back(p);
   });
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Blinded balances                                                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<blinded_balance_object> database_api::get_blinded_balances( const flat_set<commitment_type>& commitments )const
{
   return my->get_blinded_balances( commitments );
}

vector<blinded_balance_object> database_api_impl::get_blinded_balances( const flat_set<commitment_type>& commitments )const
{
   vector<blinded_balance_object> result; result.reserve(commitments.size());
   const auto& bal_idx = _db.get_index_type<blinded_balance_index>();
   const auto& by_commitment_idx = bal_idx.indices().get<by_commitment>();
   for( const auto& c : commitments )
   {
      auto itr = by_commitment_idx.find( c );
      if( itr != by_commitment_idx.end() )
         result.push_back( *itr );
   }
   return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Licenses:                                                        //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<license_type_object> database_api::get_license_type(license_type_id_type license_id) const
{
  return my->get_license_type(license_id);
}

optional<license_type_object> database_api_impl::get_license_type(license_type_id_type license_id) const
{
  return _dal.get_license_type(license_id);
}

vector<license_type_object> database_api_impl::get_license_types() const
{
   const auto& idx = _db.get_index_type<license_type_index>().indices().get<by_id>();
   return vector<license_type_object>(idx.begin(), idx.end());
}

vector<license_type_object> database_api::get_license_types() const
{
   return my->get_license_types();
}

vector<pair<string, license_type_id_type>> database_api::get_license_type_names_ids() const
{
    return my->get_license_type_names_ids();
}

vector<pair<string, license_type_id_type>> database_api_impl::get_license_type_names_ids() const
{
    return _dal.get_license_type_names_ids();
}

vector<license_types_grouped_by_kind_res> database_api::get_license_type_names_ids_grouped_by_kind() const
{
    return my->get_license_type_names_ids_grouped_by_kind();
}

vector<license_types_grouped_by_kind_res> database_api_impl::get_license_type_names_ids_grouped_by_kind() const
{
    return _dal.get_license_type_names_ids_grouped_by_kind();
}

vector<license_objects_grouped_by_kind_res> database_api::get_license_objects_grouped_by_kind() const
{
    return my->get_license_objects_grouped_by_kind();
}

vector<license_objects_grouped_by_kind_res> database_api_impl::get_license_objects_grouped_by_kind() const
{
    return _dal.get_license_objects_grouped_by_kind();
}

vector<optional<license_type_object>> database_api_impl::get_license_types(const vector<license_type_id_type>& license_type_ids) const
{
   vector<optional<license_type_object>> result;
   result.reserve(license_type_ids.size());
   std::transform(license_type_ids.begin(), license_type_ids.end(), std::back_inserter(result),
                  [this](license_type_id_type id) -> optional<license_type_object> {
      if(auto o = _db.find(id))
      {
         subscribe_to_item( id );
         return *o;
      }
      return {};
   });
   return result;
}

vector<license_type_object> database_api::list_license_types_by_name( const string& lower_bound_name,
                                                                      uint32_t limit ) const
{
   FC_ASSERT( limit <= 100 );
   return my->list_bounded_objects_indexed_by_string<license_type_index, by_name>( lower_bound_name, limit );
}

vector<license_type_object> database_api::list_license_types_by_amount( const uint32_t lower_bound_amount,
                                                                        uint32_t limit ) const
{
   FC_ASSERT( limit <= 100 );
   return my->list_bounded_objects_indexed_by_num<license_type_index, by_amount>( lower_bound_amount, limit );
}

vector<optional<license_type_object>> database_api::lookup_license_type_names(const vector<string>& names_or_ids)const
{
   return my->lookup_string_or_id<license_type_id_type, license_type_index, by_name>( names_or_ids );
}

vector<optional<license_information_object>> database_api::get_license_information(const vector<account_id_type>& account_ids) const
{
    return my->get_license_information(account_ids);
}

vector<optional<license_information_object>> database_api_impl::get_license_information(const vector<account_id_type>& account_ids) const
{
   vector<optional<license_information_object>> result;
   result.reserve(account_ids.size());
   std::transform(account_ids.begin(), account_ids.end(), std::back_inserter(result),
                  [this](account_id_type id) -> optional<license_information_object> {
      auto acc = _db.find(id);
      if( acc && acc->license_information.valid() )
         return {(*acc->license_information)(_db)};
      return {};
   });
   return result;
}

vector<upgrade_event_object> database_api::get_upgrade_events() const
{
  return my->get_upgrade_events();
}

vector<upgrade_event_object> database_api_impl::get_upgrade_events() const
{
  const auto& idx = _db.get_index_type<upgrade_event_index>().indices().get<by_id>();
  return vector<upgrade_event_object>(idx.begin(), idx.end());
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Cycles:                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

acc_id_share_t_res database_api::get_free_cycle_balance(const account_id_type id)const
{
   return my->get_free_cycle_balance(id);
}

acc_id_share_t_res database_api_impl::get_free_cycle_balance(const account_id_type id) const
{
   return _dal.get_free_cycle_balance(id);
}

acc_id_vec_cycle_agreement_res database_api::get_all_cycle_balances(account_id_type id) const
{
    return my->get_all_cycle_balances(id);
}

acc_id_vec_cycle_agreement_res database_api_impl::get_all_cycle_balances(account_id_type id) const
{
    return _dal.get_all_cycle_balances(id);
}

acc_id_share_t_res database_api::get_dascoin_balance(account_id_type id) const
{
    return my->get_dascoin_balance(id);
}

acc_id_share_t_res database_api_impl::get_dascoin_balance(account_id_type id) const
{
    return _dal.get_dascoin_balance(id);
}

vector<acc_id_share_t_res> database_api::get_free_cycle_balances_for_accounts(vector<account_id_type> ids) const
{
    return my->get_free_cycle_balances_for_accounts(ids);
}

vector<acc_id_share_t_res> database_api_impl::get_free_cycle_balances_for_accounts(vector<account_id_type> ids) const
{
    return _dal.get_free_cycle_balances_for_accounts(ids);
}

vector<acc_id_vec_cycle_agreement_res> database_api::get_all_cycle_balances_for_accounts(vector<account_id_type> ids) const
{
    return my->get_all_cycle_balances_for_accounts(ids);
}

vector<acc_id_vec_cycle_agreement_res> database_api_impl::get_all_cycle_balances_for_accounts(vector<account_id_type> ids) const
{
    return _dal.get_all_cycle_balances_for_accounts(ids);
}

vector<acc_id_share_t_res> database_api::get_dascoin_balances_for_accounts(vector<account_id_type> ids) const
{
    return my->get_dascoin_balances_for_accounts(ids);
}

vector<acc_id_share_t_res> database_api_impl::get_dascoin_balances_for_accounts(vector<account_id_type> ids) const
{
    return _dal.get_dascoin_balances_for_accounts(ids);
}

vector<reward_queue_object> database_api::get_reward_queue() const
{
   return my->get_reward_queue();
}

vector<reward_queue_object> database_api_impl::get_reward_queue() const
{
   return _dal.get_reward_queue();
}
vector<reward_queue_object> database_api::get_reward_queue_by_page(uint32_t from, uint32_t amount) const
{
   return my->get_reward_queue_by_page(from, amount);
}

vector<reward_queue_object> database_api_impl::get_reward_queue_by_page(uint32_t from, uint32_t amount) const
{
   return _dal.get_reward_queue_by_page(from, amount);
}

uint32_t database_api::get_reward_queue_size() const
{
   return my->get_reward_queue_size();
}

uint32_t database_api_impl::get_reward_queue_size() const
{
   return _dal.get_reward_queue_size();
}

acc_id_queue_subs_w_pos_res database_api::get_queue_submissions_with_pos(account_id_type account_id) const
{
    return my->get_queue_submissions_with_pos(account_id);
}

acc_id_queue_subs_w_pos_res database_api_impl::get_queue_submissions_with_pos(account_id_type account_id) const
{
    return _dal.get_queue_submissions_with_pos(account_id);
}

vector<acc_id_queue_subs_w_pos_res>
    database_api::get_queue_submissions_with_pos_for_accounts(vector<account_id_type> ids) const
{
    return my->get_queue_submissions_with_pos_for_accounts(ids);
}

vector<acc_id_queue_subs_w_pos_res>
    database_api_impl::get_queue_submissions_with_pos_for_accounts(vector<account_id_type> ids) const
{
    return _dal.get_queue_submissions_with_pos_for_accounts(ids);
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// REQUESTS:                                                        //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<issue_asset_request_object> database_api::get_all_webasset_issue_requests() const
{
   return my->list_all_objects<issue_asset_request_index, by_expiration>();
}

vector<wire_out_holder_object> database_api::get_all_wire_out_holders() const
{
   return my->list_all_objects<wire_out_holder_index, by_id>();
}

vector<wire_out_with_fee_holder_object> database_api::get_all_wire_out_with_fee_holders() const
{
  return my->list_all_objects<wire_out_with_fee_holder_index, by_id>();
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// VAULTS:                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

optional<vault_info_res> database_api::get_vault_info(account_id_type vault_id) const
{
    return my->get_vault_info(vault_id);
}

optional<vault_info_res> database_api_impl::get_vault_info(account_id_type vault_id) const
{
    return _dal.get_vault_info(vault_id);
}

vector<acc_id_vault_info_res> database_api::get_vaults_info(vector<account_id_type> vault_ids) const
{
    return my->get_vaults_info(vault_ids);
}

vector<acc_id_vault_info_res> database_api_impl::get_vaults_info(vector<account_id_type> vault_ids) const
{
    return _dal.get_vaults_info(vault_ids);
}

optional<cycle_price> database_api::calculate_cycle_price(share_type cycle_amount, asset_id_type asset_id) const
{
    return my->calculate_cycle_price(cycle_amount, asset_id);
}

optional<cycle_price> database_api_impl::calculate_cycle_price(share_type cycle_amount, asset_id_type asset_id) const
{
    // For now we can only buy cycles with dascoin
    if (asset_id != _db.get_dascoin_asset_id())
        return {};

    const dynamic_global_property_object dgpo = get_dynamic_global_properties();
    const auto& asset_obj = asset_id(_db);

    double price = static_cast<double>(cycle_amount.value) / (static_cast<double>(dgpo.frequency.value) / DASCOIN_FREQUENCY_PRECISION);
    price = std::ceil(price * std::pow(10, asset_obj.precision)) / std::pow(10, asset_obj.precision);
    return cycle_price{cycle_amount, asset(price * std::pow(10, asset_obj.precision), asset_obj.id), dgpo.frequency};
}

vector<dasc_holder> database_api::get_top_dasc_holders() const
{
    return my->get_top_dasc_holders();
}

vector<dasc_holder> database_api_impl::get_top_dasc_holders() const
{
    static const uint32_t max_holders = 100;
    vector<dasc_holder> tmp;
    const auto& dasc_id = _db.get_dascoin_asset_id();
    const auto& idx = _db.get_index_type<account_index>().indices().get<by_id>();
    for ( auto it = idx.cbegin(); it != idx.cend(); ++it )
    {
        const auto& account = *it;
        dasc_holder holder;
        holder.holder = account.id;
        if (account.kind == account_kind::wallet)
        {
            holder.vaults = account.vault.size();
            const auto& balance_obj = _db.get_balance_object(account.id, dasc_id);
            holder.amount = balance_obj.balance + balance_obj.reserved;
            std::for_each(account.vault.begin(), account.vault.end(), [this, &holder, &dasc_id](const account_id_type& vault_id) {
                const auto& balance_obj = _db.get_balance_object(vault_id, dasc_id);
                holder.amount += balance_obj.balance;
            });
            tmp.emplace_back(holder);
        }
        else if (account.kind == account_kind::custodian || (account.kind == account_kind::vault && account.parents.empty()))
        {
            holder.vaults = 0;
            const auto& balance_obj = _db.get_balance_object(account.id, dasc_id);
            holder.amount = balance_obj.balance;
            tmp.emplace_back(holder);
        }
    }

    std::partial_sort(tmp.begin(), tmp.begin() + max_holders, tmp.end(), [](dasc_holder& a, dasc_holder& b) {
        return a.amount > b.amount;
    });
    vector<dasc_holder> ret(tmp.begin(), tmp.begin() + max_holders);
    return ret;
}

optional<withdrawal_limit> database_api::get_withdrawal_limit(account_id_type account, asset_id_type asset_id) const
{
    return my->get_withdrawal_limit(account, asset_id);
}

optional<withdrawal_limit> database_api_impl::get_withdrawal_limit(account_id_type account, asset_id_type asset_id) const
{
    // Do we have a price for this asset?
    auto p = _db.get_price_in_web_eur(asset_id);
    if (!p.valid())
        return {};

    const auto& global_parameters_ext = _db.get_global_properties().parameters.extensions;
    auto withdrawal_limit_it = std::find_if(global_parameters_ext.begin(), global_parameters_ext.end(),
                                            [](const chain_parameters::chain_parameters_extension& ext){
                                                    return ext.which() == chain_parameters::chain_parameters_extension::tag< withdrawal_limit_type >::value;
                                            });
    // Is withdrawal limit set?
    if (withdrawal_limit_it == global_parameters_ext.end())
        return {};

    auto& limit = (*withdrawal_limit_it).get<withdrawal_limit_type>();

    // Is asset limited?
    if (limit.limited_assets.find(asset_id) == limit.limited_assets.end())
        return {};

    const auto& idx = _db.get_index_type<account_index>().indices().get<by_id>();
    auto itr = idx.find(account);
    if (itr == idx.end() || itr->kind != account_kind::wallet)
        return {};

    const auto& idx2 = _db.get_index_type<withdrawal_limit_index>().indices().get<by_account_id>();
    auto itr2 = idx2.find(account);
    if (itr2 == idx2.end())
        return withdrawal_limit{limit.limit * *p, asset{0, asset_id}, _db.head_block_time(), {}};
    
    bool reset_limit = (_db.head_block_time() - itr2->beginning_of_withdrawal_interval > fc::microseconds(static_cast<int64_t>(limit.duration) * 1000000));
    asset spent;
    fc::time_point_sec when;
    if (reset_limit)
    {
        spent = asset{0, asset_id};
        when = _db.head_block_time();
    }
    else
    {
        spent = itr2->spent * *p;
        when = itr2->beginning_of_withdrawal_interval;
    }
    return withdrawal_limit{itr2->limit * *p, spent, when, itr2->last_withdrawal};
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// DASPAY:                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<payment_service_provider_object> database_api::get_payment_service_providers() const
{
    return my->list_all_objects<payment_service_provider_index, by_payment_service_provider>();
}

optional<vector<daspay_authority>> database_api::get_daspay_authority_for_account(account_id_type account) const
{
    return my->get_daspay_authority_for_account(account);
}

optional<vector<daspay_authority>> database_api_impl::get_daspay_authority_for_account(account_id_type account) const
{
    const auto& idx = _db.get_index_type<daspay_authority_index>().indices().get<by_daspay_user>();
    auto it = idx.lower_bound(account);
    const auto& it_end = idx.upper_bound(account);

    if (it == idx.end())
    {
        return {};
    }

    vector<daspay_authority> ret;
    std::transform(it, it_end, std::back_inserter(ret), [](const daspay_authority_object& dao) -> daspay_authority {
      return daspay_authority{dao.payment_provider, dao.daspay_public_key, dao.memo};
    });

    return ret;
}

vector<delayed_operation_object> database_api::get_delayed_operations_for_account(account_id_type account) const
{
  return my->get_delayed_operations_for_account(account);
}

vector<delayed_operation_object> database_api_impl::get_delayed_operations_for_account(account_id_type account) const
{
  const auto& delayed_operations = _db.get_index_type<delayed_operations_index>().indices().get<by_account>();

  vector<delayed_operation_object> result;
  for (const delayed_operation_object& operation: delayed_operations)
  {
    if (operation.account == account)
      result.emplace_back(operation);
  }
  
  return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// DAS33:                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<das33_pledge_holder_object> database_api::get_das33_pledges(das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const
{
    return my->get_das33_pledges(from, limit, phase);
}

das33_pledges_by_account_result database_api::get_das33_pledges_by_account(account_id_type account) const
{
    return my->get_das33_pledges_by_account(account);
}

vector<das33_pledge_holder_object> database_api::get_das33_pledges_by_project(das33_project_id_type project, das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const
{
    return my->get_das33_pledges_by_project(project, from, limit, phase);
}

vector<das33_pledge_holder_object> database_api_impl::get_das33_pledges(das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const
{
    FC_ASSERT( limit <= 100 );
    vector<das33_pledge_holder_object> result;

    auto default_pledge_id = das33_pledge_holder_id_type();

    const auto& pledges = _db.get_index_type<das33_pledge_holder_index>().indices().get<by_id>();
    for( auto itr = pledges.lower_bound(from); limit && itr != pledges.end(); ++itr )
    {
      if (itr->id != default_pledge_id)
      {
          if (phase && *phase != itr->phase_number)
              continue;
          result.emplace_back(*itr);
          limit--;
      }

    }

    return result;
}

das33_pledges_by_account_result database_api_impl::get_das33_pledges_by_account(account_id_type account) const
{
    vector<das33_pledge_holder_object> pledges;
    map<das33_project_id_type, share_type> total;
    map<das33_project_id_type, share_type> last_round;

    const auto& idx = _db.get_index_type<das33_pledge_holder_index>().indices().get<by_user>().equal_range(account);
    std::copy(idx.first, idx.second, std::back_inserter(pledges));

    map<das33_project_id_type, share_type> last_round_number;
    for (u_int i = 0; i < pledges.size(); i++)
    {
      das33_project_id_type project_id = pledges[i].project_id;
      share_type round_number = pledges[i].phase_number;
      if (total.find(project_id) != total.end())
      {
         total[project_id] = total[project_id] + pledges[i].base_expected.amount + pledges[i].bonus_expected.amount;
      }
      else
      {
        total[project_id] = pledges[i].base_expected.amount + pledges[i].bonus_expected.amount;
      }
      if (last_round_number.find(project_id) != last_round_number.end())
      {
        if (last_round_number[project_id] < round_number)
          last_round_number[project_id] = round_number;
      }
      else
      {
          last_round_number[project_id] = round_number;
      }
    }
    for (u_int j = 0; j < pledges.size(); j++)
    {
      das33_project_id_type project_id = pledges[j].project_id;
      share_type round_number = pledges[j].phase_number;
      if (round_number == last_round_number[project_id])
      {
        if (last_round.find(project_id) != last_round.end())
          last_round[project_id] = last_round[project_id] + pledges[j].base_expected.amount;
        else
          last_round[project_id] = pledges[j].base_expected.amount;
      }
    }

    das33_pledges_by_account_result result;
    result.pledges = pledges;
    result.total_expected = total;
    result.base_expected_in_last_round = last_round;
    return result;
}

vector<das33_pledge_holder_object> database_api_impl::get_das33_pledges_by_project(das33_project_id_type project, das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const
{
    FC_ASSERT( limit <= 100 );
    vector<das33_pledge_holder_object> result;

    auto default_pledge_id = das33_pledge_holder_id_type();

    const auto& pledges = _db.get_index_type<das33_pledge_holder_index>().indices().get<by_project>();
    for( auto itr = pledges.lower_bound(make_tuple(project, from)); limit && itr->project_id == project && itr != pledges.end(); ++itr )
    {
       if (itr->id != default_pledge_id)
       {
           if (phase && *phase != itr->phase_number)
               continue;
           result.emplace_back(*itr);
           limit--;
       }
    }

    return result;
}


vector<das33_project_object> database_api::get_das33_projects(const string& lower_bound_name, uint32_t limit) const
{
  return my->get_das33_projects(lower_bound_name, limit);
}


vector<das33_project_object> database_api_impl::get_das33_projects(const string& lower_bound_name, uint32_t limit) const
{
  FC_ASSERT( limit <= 100 );
  const auto& projects_by_name = _db.get_index_type<das33_project_index>().indices().get<by_project_name>();
  vector<das33_project_object> result;

  auto default_project_id = das33_project_id_type();

  for( auto itr = projects_by_name.lower_bound(lower_bound_name);
       limit-- && itr != projects_by_name.end();
       ++itr )
  {
     if (itr->id != default_project_id)
       result.emplace_back(*itr);
  }

  return result;
}

vector<asset> database_api::get_amount_of_assets_pledged_to_project(das33_project_id_type project) const
{
  return my->get_amount_of_assets_pledged_to_project(project);
}

vector<asset> database_api_impl::get_amount_of_assets_pledged_to_project(das33_project_id_type project) const
{
  vector<asset> result;
  map<asset_id_type, int> index_map;

  auto default_pledge_id = das33_pledge_holder_id_type();

  const auto& pledges = _db.get_index_type<das33_pledge_holder_index>().indices().get<by_project>();
  for( auto itr = pledges.lower_bound(project); itr != pledges.upper_bound(project); ++itr )
  {
   if (itr->id != default_pledge_id)
   {
     if (index_map.find(itr->pledged.asset_id) != index_map.end())
     {
       result[index_map[itr->pledged.asset_id]] += itr->pledged;
     }
     else
     {
       index_map[itr->pledged.asset_id] = result.size();
       result.emplace_back(itr->pledged);
     }
   }
  }

  return result;
}

vector<asset> database_api::get_amount_of_assets_pledged_to_project_in_phase(das33_project_id_type project, uint32_t phase) const
{
    return my->get_amount_of_assets_pledged_to_project_in_phase(project, phase);
}

vector<asset> database_api_impl::get_amount_of_assets_pledged_to_project_in_phase(das33_project_id_type project, uint32_t phase) const
{
    vector<asset> result;
    map<asset_id_type, int> index_map;

    // Get project
    const auto& idx = _db.get_index_type<das33_project_index>().indices().get<by_id>();
    auto project_iterator = idx.find(project);
    auto project_object = &(*project_iterator);
    result.emplace_back(asset{0, project_object->token_id});
    result.emplace_back(asset{0, project_object->token_id});
    index_map[project_object->token_id] = 0;

    auto default_pledge_id = das33_pledge_holder_id_type();

    const auto& pledges = _db.get_index_type<das33_pledge_holder_index>().indices().get<by_project>();
    for( auto itr = pledges.lower_bound(project); itr != pledges.upper_bound(project); ++itr )
    {
        if (itr->id != default_pledge_id && itr->phase_number == phase)
        {
            if (index_map.find(itr->pledged.asset_id) != index_map.end())
            {
                result[index_map[itr->pledged.asset_id]] += itr->pledged;
            }
            else
            {
                index_map[itr->pledged.asset_id] = result.size();
                result.emplace_back(itr->pledged);
            }
            result[index_map[project_object->token_id]] += (itr->base_expected + itr->bonus_expected);
            result[1] += itr->base_expected;// * project_object->token_price;
        }
    }

    result[1] = result[1] * project_object->token_price;

    return result;
}

das33_project_tokens_amount database_api::get_amount_of_project_tokens_received_for_asset(das33_project_id_type project, asset to_pledge) const
{
  return my->get_amount_of_project_tokens_received_for_asset(project, to_pledge);
}

das33_project_tokens_amount database_api_impl::get_amount_of_project_tokens_received_for_asset(das33_project_id_type project, asset to_pledge) const
{
  const auto& project_obj = project(_db);

  share_type precision = graphene::chain::precision_modifier(to_pledge.asset_id(_db), _db.get_web_asset_id()(_db));

  const auto& asset_price = graphene::chain::calculate_price(to_pledge.asset_id, project, _db);
  FC_ASSERT(asset_price.valid(), "There is no proper price for ${asset}", ("asset", to_pledge.asset_id));

  asset base_asset = graphene::chain::asset_price_multiply(to_pledge, precision.value, *asset_price, project_obj.token_price);

  asset bonus;
  auto discount_iterator = project_obj.discounts.find(to_pledge.asset_id);
  if ( discount_iterator != project_obj.discounts.end())
  {
    bonus.amount = (base_asset.amount * 100 / discount_iterator->second) - base_asset.amount;
    bonus.asset_id = base_asset.asset_id;
  }

  das33_project_tokens_amount result = das33_project_tokens_amount(to_pledge, base_asset, bonus);
  return result;
}

das33_project_tokens_amount database_api::get_amount_of_asset_needed_for_project_token(das33_project_id_type project, asset_id_type asset_id, asset tokens) const
{
  return my->get_amount_of_asset_needed_for_project_token(project, asset_id, tokens);
}

das33_project_tokens_amount database_api_impl::get_amount_of_asset_needed_for_project_token(das33_project_id_type project, asset_id_type asset_id, asset tokens) const
{
  const auto& project_obj = project(_db);

  share_type precision = graphene::chain::precision_modifier(tokens.asset_id(_db), _db.get_web_asset_id()(_db));

  const auto& asset_price = graphene::chain::calculate_price(asset_id, project, _db);
  FC_ASSERT(asset_price.valid(), "There is no proper price for ${asset}", ("asset", asset_id));

  asset to_pledge = graphene::chain::asset_price_multiply(tokens, precision.value, project_obj.token_price, *asset_price);

  asset bonus;
  auto discount_iterator = project_obj.discounts.find(asset_id);
  if ( discount_iterator != project_obj.discounts.end())
  {
    bonus.amount = (tokens.amount * 100 / discount_iterator->second) - tokens.amount;
    bonus.asset_id = tokens.asset_id;
  }

  das33_project_tokens_amount result = das33_project_tokens_amount(to_pledge, tokens, bonus);
  return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Prices:                                                          //
//                                                                  //
//////////////////////////////////////////////////////////////////////

vector<last_price_object> database_api::get_last_prices() const
{
  return my->get_last_prices();
}

vector<last_price_object> database_api_impl::get_last_prices() const
{
  vector<last_price_object> result;
  const auto& idx = _db.get_index_type<last_price_index>().indices().get<by_market_key>();
  for (auto itr = idx.begin(); itr != idx.end(); itr++)
    result.emplace_back(*itr);
  return result;
}

vector<external_price_object> database_api::get_external_prices() const
{
  return my->get_external_prices();
}

vector<external_price_object> database_api_impl::get_external_prices() const
{
  vector<external_price_object> result;
  const auto& idx = _db.get_index_type<external_price_index>().indices().get<by_market_key>();
  for (auto itr = idx.begin(); itr != idx.end(); itr++)
    result.emplace_back(*itr);
  return result;
}

//////////////////////////////////////////////////////////////////////
//                                                                  //
// Private methods                                                  //
//                                                                  //
//////////////////////////////////////////////////////////////////////

void database_api_impl::broadcast_updates( const vector<variant>& updates )
{
   if( updates.size() && _subscribe_callback ) {
      auto capture_this = shared_from_this();
      fc::async([capture_this,updates](){
          if(capture_this->_subscribe_callback)
             capture_this->_subscribe_callback( fc::variant(updates) );
      });
   }
}

void database_api_impl::broadcast_market_updates( const market_queue_type& queue)
{
   if( queue.size() )
   {
      auto capture_this = shared_from_this();
      fc::async([capture_this, this, queue]() {
          for (const auto &item : queue)
          {
              auto sub = _market_subscriptions.find(item.first);
              if (sub != _market_subscriptions.end())
                  sub->second(fc::variant(item.second));
          }
      });
   }
}

void database_api_impl::on_objects_removed( const vector<object_id_type>& ids, const vector<const object*>& objs, const flat_set<account_id_type>& impacted_accounts )
{
   handle_object_changed(_notify_remove_create, false, ids, impacted_accounts, 
      [objs](object_id_type id) -> const object* {
         auto it = std::find_if(objs.begin(), objs.end(), [id](const object* o) {return o != nullptr && o->id == id;});
         if (it != objs.end())
         {
            return *it;
         }
         return nullptr;
   });
}

void database_api_impl::on_objects_new(const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts)
{
   handle_object_changed(_notify_remove_create, true, ids, impacted_accounts,
      std::bind(&object_database::find_object, &_db, std::placeholders::_1)
   );
}

void database_api_impl::on_objects_changed(const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts)
{
   handle_object_changed(false, true, ids, impacted_accounts,
      std::bind(&object_database::find_object, &_db, std::placeholders::_1)
   );
}

void database_api_impl::handle_object_changed(bool force_notify, bool full_object, const vector<object_id_type>& ids, const flat_set<account_id_type>& impacted_accounts, std::function<const object*(object_id_type id)> find_object)
{
   if( _subscribe_callback )
   {
      vector<variant> updates;

      for(auto id : ids)
      {
         const object* obj = nullptr;
         if( force_notify || is_subscribed_to_item(id) || is_impacted_account(impacted_accounts) )
         {
            if ( full_object )
            {
               obj = find_object(id);
               if( obj )
               {
                  updates.emplace_back( obj->to_variant() );
               }
            }
            else
            {
               updates.emplace_back( fc::variant( id, 1 ) );
            }
         }
      }

      if( updates.size() )
         broadcast_updates(updates);
   }
   if( _market_subscriptions.size() )
   {
      market_queue_type broadcast_queue;

      for(auto id : ids)
      {
         if( id.is<call_order_object>() )
         {
            enqueue_if_subscribed_to_market<call_order_object>( find_object(id), broadcast_queue, full_object );
         }
         else if( id.is<limit_order_object>() )
         {
            enqueue_if_subscribed_to_market<limit_order_object>( find_object(id), broadcast_queue, full_object );
         }
      }

      if( broadcast_queue.size() )
         broadcast_market_updates(broadcast_queue);
   }
}

/** note: this method cannot yield because it is called in the middle of
 * apply a block.
 */
void database_api_impl::on_applied_block()
{
   if (_block_applied_callback)
   {
      auto capture_this = shared_from_this();
      block_id_type block_id = _db.head_block_id();
      fc::async([this,capture_this,block_id](){
         _block_applied_callback(fc::variant(block_id, 1));
      });
   }

   if(_market_subscriptions.size() == 0)
      return;

   const auto& ops = _db.get_applied_operations();
   map< std::pair<asset_id_type,asset_id_type>, vector<pair<operation, operation_result>> > subscribed_markets_ops;
   for(const optional< operation_history_object >& o_op : ops)
   {
      if( !o_op.valid() )
         continue;
      const operation_history_object& op = *o_op;

      std::pair<asset_id_type,asset_id_type> market;
      switch(op.op.which())
      {
         /*  This is sent via the object_changed callback
         case operation::tag<limit_order_create_operation>::value:
            market = op.op.get<limit_order_create_operation>().get_market();
            break;
         */
         case operation::tag<fill_order_operation>::value:
            market = op.op.get<fill_order_operation>().get_market();
            break;
            /*
         case operation::tag<limit_order_cancel_operation>::value:
         */
         default: break;
      }
      if(_market_subscriptions.count(market))
         subscribed_markets_ops[market].push_back(std::make_pair(op.op, op.result));
   }
   /// we need to ensure the database_api is not deleted for the life of the async operation
   auto capture_this = shared_from_this();
   fc::async([this,capture_this,subscribed_markets_ops](){
      for(auto item : subscribed_markets_ops)
      {
         auto itr = _market_subscriptions.find(item.first);
         if(itr != _market_subscriptions.end())
            itr->second(fc::variant(item.second, GRAPHENE_NET_MAX_NESTED_OBJECTS));
      }
   });
}

} } // graphene::app
