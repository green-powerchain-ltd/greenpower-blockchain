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
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <list>

#include <boost/version.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/range/algorithm/unique.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <fc/git_revision.hpp>
#include <fc/io/fstream.hpp>
#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/cli.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>

#include <graphene/app/api.hpp>
#include <graphene/chain/access_layer.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/utilities/git_revision.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/utilities/words.hpp>
#include <graphene/wallet/wallet.hpp>
#include <graphene/wallet/api_documentation.hpp>
#include <graphene/wallet/reflect_util.hpp>
#include <graphene/debug_witness/debug_api.hpp>
#include <fc/smart_ref_impl.hpp>

#ifndef WIN32
# include <sys/types.h>
# include <sys/stat.h>
#endif

#define BRAIN_KEY_WORD_COUNT 16

namespace graphene { namespace wallet {

namespace detail {

class operation_printer;
struct operation_result_printer
{
public:
   operation_result_printer( operation_printer* op )
   {
      _operation_printer = op;
   }
   operation_printer* _operation_printer;
   typedef std::string result_type;

   std::string operator()(const void_result& x) const;
   std::string operator()(const object_id_type& oid);
   std::string operator()(const asset& a);
};

// BLOCK  TRX  OP  VOP
struct operation_printer
{
private:
   ostream& out;
   const wallet_api_impl& wallet;
   operation_result result;

   bool use_cache;
   std::map<account_id_type, account_object> cached_accounts;
   std::map<asset_id_type, asset_object> cached_assets;

   std::string fee(const asset& a);

public:
   operation_printer( ostream& out, const wallet_api_impl& wallet, const operation_result& r = operation_result())
      : out(out),
        wallet(wallet),
        result(r),
        use_cache(false)
   {}
   operation_printer( ostream& out, const wallet_api_impl& wallet
                           , const operation_result& r
                           , bool use_cache
                           , std::map<account_id_type, account_object>& cached_accounts
                           , std::map<asset_id_type, asset_object>& cached_assets
                           )
         : out(out),
           wallet(wallet),
           result(r),
           use_cache(use_cache),
           cached_accounts(cached_accounts),
           cached_assets(cached_assets)
      {}
   typedef std::string result_type;

   template<typename T>
   std::string operator()(const T& op);

   std::string operator()(const transfer_operation& op);
   std::string operator()(const transfer_from_blind_operation& op);
   std::string operator()(const transfer_to_blind_operation& op);
   std::string operator()(const account_create_operation& op);
   std::string operator()(const account_update_operation& op);
   std::string operator()(const asset_create_operation& op);
   std::string operator()(const daspay_debit_account_operation& op);

   asset_object get_asset(asset_id_type id);
   account_object get_account(account_id_type id);

};

struct account_asset_result_cache_visitor;
struct account_asset_cache_visitor
{

public:
   account_asset_cache_visitor( std::set<asset_id_type>& asset_cache
                                 , std::set<account_id_type>& account_cache
                                 , const operation_result& r = operation_result())
      :
        asset_cache(asset_cache),
        account_cache(account_cache),
        result(r)
   {}

   typedef void result_type;

   template<typename T>
   void operator()(const T& op);

   void operator()(const transfer_operation& op);
   void operator()(const transfer_from_blind_operation& op);
   void operator()(const transfer_to_blind_operation& op);
   void operator()(const account_create_operation& op);
   void operator()(const account_update_operation& op);
   void operator()(const asset_create_operation& op);

   std::set<asset_id_type>& asset_cache;
   std::set<account_id_type>& account_cache;
private:
   operation_result result;
};

struct account_asset_result_cache_visitor
{
public:
   account_asset_result_cache_visitor( account_asset_cache_visitor* av )
   {
      _av = av;
   }
   account_asset_cache_visitor* _av;
   typedef void result_type;

   void operator()(const void_result& x) const;
   void operator()(const object_id_type& oid);
   void operator()(const asset& a);
};

struct fee_asset_id_visitor
{
   typedef void result_type;

   fee_schedule& schedule;

   fee_asset_id_visitor(fee_schedule& schedule)
   : schedule(schedule)
   {}

   result_type operator()( const asset_id_type& asset_id ) const
   {
      schedule.fee_asset_id = asset_id;
   }

   template<typename VariantMember>
   result_type operator()( const VariantMember& op ) const
   {
   }
};

template<class T>
optional<T> maybe_id( const string& name_or_id )
{
   if( std::isdigit( name_or_id.front() ) )
   {
      try
      {
         return fc::variant(name_or_id).as<T>( 1 );
      }
      catch (const fc::exception&)
      {
      }
   }
   return optional<T>();
}

string address_to_shorthash( const address& addr )
{
   uint32_t x = addr.addr._hash[0];
   static const char hd[] = "0123456789abcdef";
   string result;

   result += hd[(x >> 0x1c) & 0x0f];
   result += hd[(x >> 0x18) & 0x0f];
   result += hd[(x >> 0x14) & 0x0f];
   result += hd[(x >> 0x10) & 0x0f];
   result += hd[(x >> 0x0c) & 0x0f];
   result += hd[(x >> 0x08) & 0x0f];
   result += hd[(x >> 0x04) & 0x0f];
   result += hd[(x        ) & 0x0f];

   return result;
}

fc::ecc::private_key derive_private_key( const std::string& prefix_string,
                                         int sequence_number )
{
   std::string sequence_string = std::to_string(sequence_number);
   fc::sha512 h = fc::sha512::hash(prefix_string + " " + sequence_string);
   fc::ecc::private_key derived_key = fc::ecc::private_key::regenerate(fc::sha256::hash(h));
   return derived_key;
}

string normalize_brain_key( string s )
{
   size_t i = 0, n = s.length();
   std::string result;
   char c;
   result.reserve( n );

   bool preceded_by_whitespace = false;
   bool non_empty = false;
   while( i < n )
   {
      c = s[i++];
      switch( c )
      {
      case ' ':  case '\t': case '\r': case '\n': case '\v': case '\f':
         preceded_by_whitespace = true;
         continue;

      case 'a': c = 'A'; break;
      case 'b': c = 'B'; break;
      case 'c': c = 'C'; break;
      case 'd': c = 'D'; break;
      case 'e': c = 'E'; break;
      case 'f': c = 'F'; break;
      case 'g': c = 'G'; break;
      case 'h': c = 'H'; break;
      case 'i': c = 'I'; break;
      case 'j': c = 'J'; break;
      case 'k': c = 'K'; break;
      case 'l': c = 'L'; break;
      case 'm': c = 'M'; break;
      case 'n': c = 'N'; break;
      case 'o': c = 'O'; break;
      case 'p': c = 'P'; break;
      case 'q': c = 'Q'; break;
      case 'r': c = 'R'; break;
      case 's': c = 'S'; break;
      case 't': c = 'T'; break;
      case 'u': c = 'U'; break;
      case 'v': c = 'V'; break;
      case 'w': c = 'W'; break;
      case 'x': c = 'X'; break;
      case 'y': c = 'Y'; break;
      case 'z': c = 'Z'; break;

      default:
         break;
      }
      if( preceded_by_whitespace && non_empty )
         result.push_back(' ');
      result.push_back(c);
      preceded_by_whitespace = false;
      non_empty = true;
   }
   return result;
}

struct op_prototype_visitor
{
   typedef void result_type;

   int t = 0;
   flat_map< std::string, operation >& name2op;

   op_prototype_visitor(
      int _t,
      flat_map< std::string, operation >& _prototype_ops
      ):t(_t), name2op(_prototype_ops) {}

   template<typename Type>
   result_type operator()( const Type& op )const
   {
      string name = fc::get_typename<Type>::name();
      size_t p = name.rfind(':');
      if( p != string::npos )
         name = name.substr( p+1 );
      name2op[ name ] = Type();
   }
};

class wallet_api_impl
{
public:
   api_documentation method_documentation;
private:
   void claim_registered_account(const account_object& account)
   {
      auto it = _wallet.pending_account_registrations.find( account.name );
      FC_ASSERT( it != _wallet.pending_account_registrations.end() );
      for (const std::string& wif_key : it->second)
         if( !import_key( account.name, wif_key ) )
         {
            // somebody else beat our pending registration, there is
            //    nothing we can do except log it and move on
            elog( "account ${name} registered by someone else first!",
                  ("name", account.name) );
            // might as well remove it from pending regs,
            //    because there is now no way this registration
            //    can become valid (even in the extremely rare
            //    possibility of migrating to a fork where the
            //    name is available, the user can always
            //    manually re-register)
         }
      _wallet.pending_account_registrations.erase( it );
   }

   // after a witness registration succeeds, this saves the private key in the wallet permanently
   //
   void claim_registered_witness(const std::string& witness_name)
   {
      auto iter = _wallet.pending_witness_registrations.find(witness_name);
      FC_ASSERT(iter != _wallet.pending_witness_registrations.end());
      std::string wif_key = iter->second;

      // get the list key id this key is registered with in the chain
      fc::optional<fc::ecc::private_key> witness_private_key = wif_to_key(wif_key);
      FC_ASSERT(witness_private_key);

      auto pub_key = witness_private_key->get_public_key();
      _keys[pub_key] = wif_key;
      _wallet.pending_witness_registrations.erase(iter);
   }

   fc::mutex _resync_mutex;
   void resync()
   {
      fc::scoped_lock<fc::mutex> lock(_resync_mutex);
      // this method is used to update wallet_data annotations
      //   e.g. wallet has been restarted and was not notified
      //   of events while it was down
      //
      // everything that is done "incremental style" when a push
      //   notification is received, should also be done here
      //   "batch style" by querying the blockchain

      if( !_wallet.pending_account_registrations.empty() )
      {
         // make a vector of the account names pending registration
         std::vector<string> pending_account_names = boost::copy_range<std::vector<string> >(boost::adaptors::keys(_wallet.pending_account_registrations));

         // look those up on the blockchain
         std::vector<fc::optional<graphene::chain::account_object >>
               pending_account_objects = _remote_db->lookup_account_names( pending_account_names );

         // if any of them exist, claim them
         for( const fc::optional<graphene::chain::account_object>& optional_account : pending_account_objects )
            if( optional_account )
               claim_registered_account(*optional_account);
      }

      if (!_wallet.pending_witness_registrations.empty())
      {
         // make a vector of the owner accounts for witnesses pending registration
         std::vector<string> pending_witness_names = boost::copy_range<std::vector<string> >(boost::adaptors::keys(_wallet.pending_witness_registrations));

         // look up the owners on the blockchain
         std::vector<fc::optional<graphene::chain::account_object>> owner_account_objects = _remote_db->lookup_account_names(pending_witness_names);

         // if any of them have registered witnesses, claim them
         for( const fc::optional<graphene::chain::account_object>& optional_account : owner_account_objects )
            if (optional_account)
            {
               fc::optional<witness_object> witness_obj = _remote_db->get_witness_by_account(optional_account->id);
               if (witness_obj)
                  claim_registered_witness(optional_account->name);
            }
      }
   }

   void enable_umask_protection()
   {
#ifdef __unix__
      _old_umask = umask( S_IRWXG | S_IRWXO );
#endif
   }

   void disable_umask_protection()
   {
#ifdef __unix__
      umask( _old_umask );
#endif
   }

   void init_prototype_ops()
   {
      operation op;
      for( int t=0; t<op.count(); t++ )
      {
         op.set_which( t );
         op.visit( op_prototype_visitor(t, _prototype_ops) );
      }
      return;
   }

   map<transaction_handle_type, signed_transaction> _builder_transactions;

   // if the user executes the same command twice in quick succession,
   // we might generate the same transaction id, and cause the second
   // transaction to be rejected.  This can be avoided by altering the
   // second transaction slightly (bumping up the expiration time by
   // a second).  Keep track of recent transaction ids we've generated
   // so we can know if we need to do this
   struct recently_generated_transaction_record {
       fc::time_point_sec generation_time;
       graphene::chain::transaction_id_type transaction_id;

       recently_generated_transaction_record() = default;
       explicit recently_generated_transaction_record(fc::time_point_sec generation_time,
                                                      graphene::chain::transaction_id_type transaction_id)
           : generation_time(generation_time)
           , transaction_id(transaction_id)
       {
       }
   };

   struct timestamp_index{};
   typedef boost::multi_index_container<recently_generated_transaction_record,
                                        boost::multi_index::indexed_by<boost::multi_index::hashed_unique<boost::multi_index::member<recently_generated_transaction_record,
                                                                                                                                    graphene::chain::transaction_id_type,
                                                                                                                                    &recently_generated_transaction_record::transaction_id>,
                                                                                                         std::hash<graphene::chain::transaction_id_type> >,
                                                                       boost::multi_index::ordered_non_unique<boost::multi_index::tag<timestamp_index>,
                                                                                                              boost::multi_index::member<recently_generated_transaction_record, fc::time_point_sec, &recently_generated_transaction_record::generation_time> > > > recently_generated_transaction_set_type;
   recently_generated_transaction_set_type _recently_generated_transactions;

public:
   wallet_api& self;
   wallet_api_impl( wallet_api& s, const wallet_data& initial_data, fc::api<login_api> rapi )
      : self(s),
        _chain_id(initial_data.chain_id),
        _remote_api(rapi),
        _remote_db(rapi->database()),
        _remote_net_broadcast(rapi->network_broadcast()),
        _remote_hist(rapi->history())
   {
      chain_id_type remote_chain_id = _remote_db->get_chain_id();
      if( remote_chain_id != _chain_id )
      {
         FC_THROW( "Remote server gave us an unexpected chain_id",
            ("remote_chain_id", remote_chain_id)
            ("chain_id", _chain_id) );
      }
      init_prototype_ops();

      _remote_db->set_block_applied_callback( [this](const variant& block_id )
      {
         on_block_applied( block_id );
      } );

      _wallet.chain_id = _chain_id;
      _wallet.ws_server = initial_data.ws_server;
      _wallet.ws_user = initial_data.ws_user;
      _wallet.ws_password = initial_data.ws_password;
   }
   virtual ~wallet_api_impl()
   {
      try
      {
         _remote_db->cancel_all_subscriptions();
      }
      catch (const fc::exception& e)
      {
         // Right now the wallet_api has no way of knowing if the connection to the
         // witness has already disconnected (via the witness node exiting first).
         // If it has exited, cancel_all_subscriptsions() will throw and there's
         // nothing we can do about it.
         // dlog("Caught exception ${e} while canceling database subscriptions", ("e", e));
      }
   }

   void encrypt_keys()
   {
      if( !is_locked() )
      {
         plain_keys data;
         data.keys = _keys;
         data.checksum = _checksum;
         auto plain_txt = fc::raw::pack(data);
         _wallet.cipher_keys = fc::aes_encrypt( data.checksum, plain_txt );
      }
   }

   void on_block_applied( const variant& block_id )
   {
      fc::async([this]{resync();}, "Resync after block");
   }

   bool copy_wallet_file( string destination_filename )
   {
      fc::path src_path = get_wallet_filename();
      if( !fc::exists( src_path ) )
         return false;
      fc::path dest_path = destination_filename + _wallet_filename_extension;
      int suffix = 0;
      while( fc::exists(dest_path) )
      {
         ++suffix;
         dest_path = destination_filename + "-" + to_string( suffix ) + _wallet_filename_extension;
      }
      wlog( "backing up wallet ${src} to ${dest}",
            ("src", src_path)
            ("dest", dest_path) );

      fc::path dest_parent = fc::absolute(dest_path).parent_path();
      try
      {
         enable_umask_protection();
         if( !fc::exists( dest_parent ) )
            fc::create_directories( dest_parent );
         fc::copy( src_path, dest_path );
         disable_umask_protection();
      }
      catch(...)
      {
         disable_umask_protection();
         throw;
      }
      return true;
   }

   bool is_locked()const
   {
      return _checksum == fc::sha512();
   }

   template<typename T>
   T get_object(object_id<T::space_id, T::type_id, T> id)const
   {
      auto ob = _remote_db->get_objects({id}).front();
      return ob.template as<T>( GRAPHENE_MAX_NESTED_OBJECTS );
   }

   void set_operation_fees( signed_transaction& tx, const fee_schedule& s  )
   {
      const auto& params = _remote_db->get_global_properties().parameters;
      fee_schedule& tmp_fee_schedule = const_cast<fee_schedule&>(s);

      for (const auto& ext : params.extensions)
      {
         ext.visit(fee_asset_id_visitor{tmp_fee_schedule});
      }

      for( auto& op : tx.operations )
         s.set_fee(op);
   }

   variant info() const
   {
      auto chain_props = get_chain_properties();
      auto global_props = get_global_properties();
      auto dynamic_props = get_dynamic_global_properties();
      fc::mutable_variant_object result;
      result["head_block_num"] = dynamic_props.head_block_number;
      result["head_block_id"] = fc::variant(dynamic_props.head_block_id, 1);
      result["head_block_age"] = fc::get_approximate_relative_time_string(dynamic_props.time,
                                                                          time_point_sec(time_point::now()),
                                                                          " old");
      result["next_maintenance_time"] = fc::get_approximate_relative_time_string(dynamic_props.next_maintenance_time);
      result["chain_id"] = chain_props.chain_id;
      result["participation"] = (100*dynamic_props.recent_slots_filled.popcount()) / 128.0;
      result["active_witnesses"] = fc::variant(global_props.active_witnesses, GRAPHENE_MAX_NESTED_OBJECTS);
      result["active_committee_members"] = fc::variant(global_props.active_committee_members, GRAPHENE_MAX_NESTED_OBJECTS);
      return result;
   }

   variant_object about() const
   {
      string client_version( graphene::utilities::git_revision_description );
      const size_t pos = client_version.find( '/' );
      if( pos != string::npos && client_version.size() > pos )
         client_version = client_version.substr( pos + 1 );

      fc::mutable_variant_object result;
      //result["blockchain_name"]        = BLOCKCHAIN_NAME;
      //result["blockchain_description"] = BTS_BLOCKCHAIN_DESCRIPTION;
      result["client_version"]           = client_version;
      result["graphene_revision"]        = graphene::utilities::git_revision_sha;
      result["graphene_revision_age"]    = fc::get_approximate_relative_time_string( fc::time_point_sec( graphene::utilities::git_revision_unix_timestamp ) );
      result["fc_revision"]              = fc::git_revision_sha;
      result["fc_revision_age"]          = fc::get_approximate_relative_time_string( fc::time_point_sec( fc::git_revision_unix_timestamp ) );
      result["compile_date"]             = "compiled on " __DATE__ " at " __TIME__;
      result["boost_version"]            = boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".");
      result["openssl_version"]          = OPENSSL_VERSION_TEXT;

      std::string bitness = boost::lexical_cast<std::string>(8 * sizeof(int*)) + "-bit";
#if defined(__APPLE__)
      std::string os = "osx";
#elif defined(__linux__)
      std::string os = "linux";
#elif defined(_MSC_VER)
      std::string os = "win32";
#else
      std::string os = "other";
#endif
      result["build"] = os + " " + bitness;

      return result;
   }

   chain_property_object get_chain_properties() const
   {
      return _remote_db->get_chain_properties();
   }
   global_property_object get_global_properties() const
   {
      return _remote_db->get_global_properties();
   }
   dynamic_global_property_object get_dynamic_global_properties() const
   {
      return _remote_db->get_dynamic_global_properties();
   }
   account_object get_account(account_id_type id) const
   {
      if( _wallet.my_accounts.get<by_id>().count(id) )
         return *_wallet.my_accounts.get<by_id>().find(id);
      auto rec = _remote_db->get_accounts({id}).front();
      FC_ASSERT(rec);
      return *rec;
   }
   account_object get_account(string account_name_or_id) const
   {
      FC_ASSERT( account_name_or_id.size() > 0 );

      if( auto id = maybe_id<account_id_type>(account_name_or_id) )
      {
         // It's an ID
         return get_account(*id);
      } else {
         // It's a name
         if( _wallet.my_accounts.get<by_name>().count(account_name_or_id) )
         {
            auto local_account = *_wallet.my_accounts.get<by_name>().find(account_name_or_id);
            auto blockchain_account = _remote_db->lookup_account_names({account_name_or_id}).front();
            FC_ASSERT( blockchain_account );
            if (local_account.id != blockchain_account->id)
               elog("my account id ${id} different from blockchain id ${id2}", ("id", local_account.id)("id2", blockchain_account->id));
            if (local_account.name != blockchain_account->name)
               elog("my account name ${id} different from blockchain name ${id2}", ("id", local_account.name)("id2", blockchain_account->name));

            return *_wallet.my_accounts.get<by_name>().find(account_name_or_id);
         }
         auto rec = _remote_db->lookup_account_names({account_name_or_id}).front();
         FC_ASSERT( rec && rec->name == account_name_or_id );
         return *rec;
      }
   }
   account_id_type get_account_id(string account_name_or_id) const
   {
      return get_account(account_name_or_id).get_id();
   }

   optional<asset_object> find_asset(asset_id_type id)const
   {
      auto rec = _remote_db->get_assets({id}).front();
      if( rec )
         _asset_cache[id] = *rec;
      return rec;
   }

   optional<asset_object> find_asset(string asset_symbol_or_id)const
   {
      FC_ASSERT( asset_symbol_or_id.size() > 0 );

      if( auto id = maybe_id<asset_id_type>(asset_symbol_or_id) )
      {
         // It's an ID
         return find_asset(*id);
      } else {
         // It's a symbol
         auto rec = _remote_db->lookup_asset_symbols({asset_symbol_or_id}).front();
         if( rec )
         {
            if( rec->symbol != asset_symbol_or_id )
               return optional<asset_object>();

            _asset_cache[rec->get_id()] = *rec;
         }
         return rec;
      }
   }

   asset_object get_web_asset() const
   {
       return *find_asset(DASCOIN_WEBASSET_SYMBOL);
   }

   asset_object get_asset(asset_id_type id)const
   {
      auto opt = find_asset(id);
      FC_ASSERT(opt);
      return *opt;
   }
   asset_object get_asset(string asset_symbol_or_id)const
   {
      auto opt = find_asset(asset_symbol_or_id);
      FC_ASSERT(opt);
      return *opt;
   }

   asset_id_type get_asset_id(string asset_symbol_or_id) const
   {
      FC_ASSERT( asset_symbol_or_id.size() > 0 );
      vector<optional<asset_object>> opt_asset;
      if( std::isdigit( asset_symbol_or_id.front() ) )
         return fc::variant(asset_symbol_or_id, 1).as<asset_id_type>( 1 );
      opt_asset = _remote_db->lookup_asset_symbols( {asset_symbol_or_id} );
      FC_ASSERT( (opt_asset.size() > 0) && (opt_asset[0].valid()) );
      return opt_asset[0]->id;
   }

   license_type_id_type get_license_type_id(string str_or_id) const
   {
      FC_ASSERT( str_or_id.size() > 0 );
      vector<optional<license_type_object>> opt_license_type;
      if( std::isdigit( str_or_id.front() ) )
         return fc::variant(str_or_id, 1).as<license_type_id_type>( 1 );
      opt_license_type = _remote_db->lookup_license_type_names( {str_or_id} );
      FC_ASSERT( (opt_license_type.size() > 0) && (opt_license_type[0].valid()) );
      return opt_license_type[0]->id;
   }

   string                            get_wallet_filename() const
   {
      return _wallet_filename;
   }

   fc::ecc::private_key              get_private_key(const public_key_type& id)const
   {
      auto it = _keys.find(id);
      FC_ASSERT( it != _keys.end() );

      fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
      FC_ASSERT( privkey );
      return *privkey;
   }

   fc::ecc::private_key get_private_key_for_account(const account_object& account)const
   {
      vector<public_key_type> active_keys = account.active.get_keys();
      if (active_keys.size() != 1)
         FC_THROW("Expecting a simple authority with one active key");
      return get_private_key(active_keys.front());
   }

   // imports the private key into the wallet, and associate it in some way (?) with the
   // given account name.
   // @returns true if the key matches a current active/owner/memo key for the named
   //          account, false otherwise (but it is stored either way)
   bool import_key(string account_name_or_id, string wif_key)
   {
      fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
      if (!optional_private_key)
         FC_THROW("Invalid private key");
      graphene::chain::public_key_type wif_pub_key = optional_private_key->get_public_key();

      account_object account = get_account( account_name_or_id );

      // make a list of all current public keys for the named account
      flat_set<public_key_type> all_keys_for_account;
      std::vector<public_key_type> active_keys = account.active.get_keys();
      std::vector<public_key_type> owner_keys = account.owner.get_keys();
      std::copy(active_keys.begin(), active_keys.end(), std::inserter(all_keys_for_account, all_keys_for_account.end()));
      std::copy(owner_keys.begin(), owner_keys.end(), std::inserter(all_keys_for_account, all_keys_for_account.end()));
      all_keys_for_account.insert(account.options.memo_key);

      optional<vector<daspay_authority>> daspay_auth = _remote_db->get_daspay_authority_for_account( account.id );
      if (daspay_auth.valid())
      {
          std::transform(daspay_auth->begin(), daspay_auth->end(), std::inserter(all_keys_for_account, all_keys_for_account.end()), [](const daspay_authority& da) { return da.daspay_public_key; });
      }

      _keys[wif_pub_key] = wif_key;

      _wallet.update_account(account);

      _wallet.extra_keys[account.id].insert(wif_pub_key);

      return all_keys_for_account.find(wif_pub_key) != all_keys_for_account.end();
   }

   vector< signed_transaction > import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast );

   bool load_wallet_file(string wallet_filename = "")
   {
      // TODO:  Merge imported wallet with existing wallet,
      //        instead of replacing it
      if( wallet_filename == "" )
         wallet_filename = _wallet_filename;

      if( ! fc::exists( wallet_filename ) )
         return false;

      _wallet = fc::json::from_file( wallet_filename ).as< wallet_data >( 2 * GRAPHENE_MAX_NESTED_OBJECTS );
      if( _wallet.chain_id != _chain_id )
         FC_THROW( "Wallet chain ID does not match",
            ("wallet.chain_id", _wallet.chain_id)
            ("chain_id", _chain_id) );

      size_t account_pagination = 100;
      vector< account_id_type > account_ids_to_send;
      size_t n = _wallet.my_accounts.size();
      account_ids_to_send.reserve( std::min( account_pagination, n ) );
      auto it = _wallet.my_accounts.begin();

      for( size_t start=0; start<n; start+=account_pagination )
      {
         size_t end = std::min( start+account_pagination, n );
         assert( end > start );
         account_ids_to_send.clear();
         std::vector< account_object > old_accounts;
         for( size_t i=start; i<end; i++ )
         {
            assert( it != _wallet.my_accounts.end() );
            old_accounts.push_back( *it );
            account_ids_to_send.push_back( old_accounts.back().id );
            ++it;
         }
         std::vector< optional< account_object > > accounts = _remote_db->get_accounts(account_ids_to_send);
         // server response should be same length as request
         FC_ASSERT( accounts.size() == account_ids_to_send.size() );
         size_t i = 0;
         for( const optional< account_object >& acct : accounts )
         {
            account_object& old_acct = old_accounts[i];
            if( !acct.valid() )
            {
               elog( "Could not find account ${id} : \"${name}\" does not exist on the chain!", ("id", old_acct.id)("name", old_acct.name) );
               i++;
               continue;
            }
            // this check makes sure the server didn't send results
            // in a different order, or accounts we didn't request
            FC_ASSERT( acct->id == old_acct.id );
            if( fc::json::to_string(*acct) != fc::json::to_string(old_acct) )
            {
               wlog( "Account ${id} : \"${name}\" updated on chain", ("id", acct->id)("name", acct->name) );
            }
            _wallet.update_account( *acct );
            i++;
         }
      }

      return true;
   }
   void save_wallet_file(string wallet_filename = "")
   {
      //
      // Serialize in memory, then save to disk
      //
      // This approach lessens the risk of a partially written wallet
      // if exceptions are thrown in serialization
      //

      encrypt_keys();

      if( wallet_filename == "" )
         wallet_filename = _wallet_filename;

      wlog( "saving wallet to file ${fn}", ("fn", wallet_filename) );

      string data = fc::json::to_pretty_string( _wallet );
      try
      {
         enable_umask_protection();
         //
         // Parentheses on the following declaration fails to compile,
         // due to the Most Vexing Parse.  Thanks, C++
         //
         // http://en.wikipedia.org/wiki/Most_vexing_parse
         //
         fc::ofstream outfile{ fc::path( wallet_filename ) };
         outfile.write( data.c_str(), data.length() );
         outfile.flush();
         outfile.close();
         disable_umask_protection();
      }
      catch(...)
      {
         disable_umask_protection();
         throw;
      }
   }

   transaction_handle_type begin_builder_transaction()
   {
      int trx_handle = _builder_transactions.empty()? 0
                                                    : (--_builder_transactions.end())->first + 1;
      _builder_transactions[trx_handle];
      return trx_handle;
   }
   void add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op)
   {
      FC_ASSERT(_builder_transactions.count(transaction_handle));
      _builder_transactions[transaction_handle].operations.emplace_back(op);
   }
   void replace_operation_in_builder_transaction(transaction_handle_type handle,
                                                 uint32_t operation_index,
                                                 const operation& new_op)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      signed_transaction& trx = _builder_transactions[handle];
      FC_ASSERT( operation_index < trx.operations.size());
      trx.operations[operation_index] = new_op;
   }
   asset set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset = GRAPHENE_SYMBOL)
   {
      FC_ASSERT(_builder_transactions.count(handle));

      auto fee_asset_obj = get_asset(fee_asset);
      asset total_fee = fee_asset_obj.amount(0);

      auto gprops = _remote_db->get_global_properties().parameters;
      if( fee_asset_obj.get_id() != asset_id_type() )
      {
         for( auto& op : _builder_transactions[handle].operations )
            total_fee += gprops.current_fees->set_fee( op, fee_asset_obj.options.core_exchange_rate );

         FC_ASSERT((total_fee * fee_asset_obj.options.core_exchange_rate).amount <=
                   get_object<asset_dynamic_data_object>(fee_asset_obj.dynamic_asset_data_id).fee_pool,
                   "Cannot pay fees in ${asset}, as this asset's fee pool is insufficiently funded.",
                   ("asset", fee_asset_obj.symbol));
      } else {
         for( auto& op : _builder_transactions[handle].operations )
            total_fee += gprops.current_fees->set_fee( op );
      }

      return total_fee;
   }
   transaction preview_builder_transaction(transaction_handle_type handle)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      return _builder_transactions[handle];
   }

   signed_transaction sign_builder_transaction(transaction_handle_type transaction_handle,
                                               optional<vector<string>> wif_keys, bool broadcast = true)
   {
       FC_ASSERT(_builder_transactions.count(transaction_handle), "No transaction to sign with handle '${h}'",
                 ("h", transaction_handle));

       auto& tx = _builder_transactions[transaction_handle];

       if (wif_keys.valid() && !wif_keys->empty())
           _builder_transactions[transaction_handle] = sign_transaction_with_keys(tx, *wif_keys, broadcast);
       else
           _builder_transactions[transaction_handle] = sign_transaction(tx, broadcast);

       return _builder_transactions[transaction_handle];
   }

   signed_transaction propose_builder_transaction(
      transaction_handle_type handle,
      time_point_sec expiration = time_point::now() + fc::minutes(1),
      uint32_t review_period_seconds = 0, bool broadcast = true)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      proposal_create_operation op;
      op.expiration_time = expiration;
      signed_transaction& trx = _builder_transactions[handle];
      std::transform(trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_ops),
                     [](const operation& op) -> op_wrapper { return op; });
      if( review_period_seconds )
         op.review_period_seconds = review_period_seconds;
      trx.operations = {op};
      _remote_db->get_global_properties().parameters.current_fees->set_fee( trx.operations.front() );

      return trx = sign_transaction(trx, broadcast);
   }

   signed_transaction propose_builder_transaction2(
      transaction_handle_type handle,
      string account_name_or_id,
      time_point_sec expiration = time_point::now() + fc::minutes(1),
      uint32_t review_period_seconds = 0, bool broadcast = true)
   {
      FC_ASSERT(_builder_transactions.count(handle));
      proposal_create_operation op;
      op.fee_paying_account = get_account(account_name_or_id).get_id();
      op.expiration_time = expiration;
      signed_transaction& trx = _builder_transactions[handle];
      std::transform(trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_ops),
                     [](const operation& op) -> op_wrapper { return op; });
      if( review_period_seconds )
         op.review_period_seconds = review_period_seconds;
      trx.operations = {op};
      _remote_db->get_global_properties().parameters.current_fees->set_fee( trx.operations.front() );

      return trx = sign_transaction(trx, broadcast);
   }

   void remove_builder_transaction(transaction_handle_type handle)
   {
      _builder_transactions.erase(handle);
   }

   signed_transaction register_account(account_kind kind,
                                       string name,
                                       public_key_type owner,
                                       public_key_type active,
                                       bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );
      FC_ASSERT( is_valid_name(name) );

      // TODO:  process when pay_from_account is ID
      account_object registrar_account_object = this->get_account( "sys.registrar" );

      account_create_operation account_create_op;
      account_create_op.kind = static_cast<uint8_t>(kind);
      account_create_op.registrar = registrar_account_object.id;
      account_create_op.name = name;
      account_create_op.owner = authority(1, owner, 1);
      account_create_op.active = authority(1, active, 1);
      account_create_op.options.memo_key = active;

      signed_transaction tx;

      tx.operations.push_back( account_create_op );

      auto current_fees = _remote_db->get_global_properties().parameters.current_fees;
      set_operation_fees( tx, current_fees );

      vector<public_key_type> paying_keys = registrar_account_object.active.get_keys();

      auto dyn_props = get_dynamic_global_properties();
      tx.set_reference_block( dyn_props.head_block_id );
      tx.set_expiration( dyn_props.time + fc::seconds(30) );
      tx.validate();

      for( public_key_type& key : paying_keys )
      {
         auto it = _keys.find(key);
         if( it != _keys.end() )
         {
            fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
            if( !privkey.valid() )
            {
               FC_ASSERT( false, "Malformed private key in _keys" );
            }
            tx.sign( *privkey, _chain_id );
         }
      }

      if( broadcast )
         _remote_net_broadcast->broadcast_transaction( tx );
      return tx;
   } FC_CAPTURE_AND_RETHROW( (name)(owner)(active)(broadcast) ) }

   signed_transaction set_daspay_transaction_ratio(const string& authority, share_type debit_ratio, share_type credit_ratio, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      set_daspay_transaction_ratio_operation op;

      op.authority = get_account(authority).id;
      op.debit_ratio = debit_ratio;
      op.debit_ratio = credit_ratio;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(debit_ratio)(credit_ratio)(broadcast) ) }

   signed_transaction create_payment_service_provider(const string& authority, const string& payment_service_provider_account, const vector<string>& payment_service_provider_clearing_accounts, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      create_payment_service_provider_operation op;

      op.authority = get_account(authority).id;
      op.payment_service_provider_account = get_account(payment_service_provider_account).id;
      for (const auto& acc : payment_service_provider_clearing_accounts)
        op.payment_service_provider_clearing_accounts.emplace_back(get_account(acc).id);

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(payment_service_provider_account)(payment_service_provider_clearing_accounts)(broadcast) ) }

   signed_transaction update_payment_service_provider(const string& authority, const string& payment_service_provider_account, const vector<string>& payment_service_provider_clearing_accounts, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      update_payment_service_provider_operation op;

      op.authority = get_account(authority).id;
      op.payment_service_provider_account = get_account(payment_service_provider_account).id;
      for (const auto& acc : payment_service_provider_clearing_accounts)
        op.payment_service_provider_clearing_accounts.emplace_back(get_account(acc).id);

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(payment_service_provider_account)(payment_service_provider_clearing_accounts)(broadcast) ) }

   signed_transaction delete_payment_service_provider(const string& authority, const string& payment_service_provider_account, bool broadcast = false)
   { try {
     FC_ASSERT( !self.is_locked() );

     delete_payment_service_provider_operation op;

     op.authority = get_account(authority).id;
     op.payment_service_provider_account = get_account(payment_service_provider_account).id;

     signed_transaction tx;
     tx.operations.push_back(op);
     set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
     tx.validate();

     return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(payment_service_provider_account)(broadcast) ) }

   signed_transaction register_daspay_authority(const string& account, const string& payment_provider, public_key_type daspay_public_key, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_id_type account_id = get_account(account).id;
      account_id_type payment_provider_id = get_account(payment_provider).id;

      register_daspay_authority_operation rda_op;

      rda_op.issuer = account_id;
      rda_op.payment_provider = payment_provider_id;
      rda_op.daspay_public_key = daspay_public_key;

      signed_transaction tx;
      tx.operations.push_back(rda_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account)(payment_provider)(daspay_public_key)(broadcast) ) }

   signed_transaction unregister_daspay_authority(const string& account, const string& payment_provider, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_id_type account_id = get_account(account).id;
      account_id_type payment_provider_id = get_account(payment_provider).id;

      unregister_daspay_authority_operation urda_op;

      urda_op.issuer = account_id;
      urda_op.payment_provider = payment_provider_id;

      signed_transaction tx;
      tx.operations.push_back(urda_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account)(payment_provider)(broadcast) ) }

   signed_transaction reserve_asset_on_account(const string& account, const string& asset_amount, const string& asset_symbol, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_id_type account_id = get_account(account).id;

      reserve_asset_on_account_operation reserve_op;

      reserve_op.account = account_id;
      reserve_op.asset_to_reserve = get_asset(asset_symbol).amount_from_string(asset_amount);

      signed_transaction tx;
      tx.operations.push_back(reserve_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account)(asset_amount)(asset_symbol)(broadcast) ) }

   signed_transaction unreserve_asset_on_account(const string& account, const string& asset_amount, const string& asset_symbol, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_id_type account_id = get_account(account).id;

      unreserve_asset_on_account_operation unreserve_op;

      unreserve_op.account = account_id;
      unreserve_op.asset_to_unreserve = get_asset(asset_symbol).amount_from_string(asset_amount);

      signed_transaction tx;
      tx.operations.push_back(unreserve_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account)(asset_amount)(asset_symbol)(broadcast) ) }

   signed_transaction daspay_debit_account(const string& payment_service_provider_account,
                                           const public_key_type& auth_key,
                                           const string& user_account,
                                           const string& asset_amount,
                                           const string& asset_symbol,
                                           const string& clearing_account,
                                           const string& transaction_id,
                                           optional<string> details,
                                           bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      daspay_debit_account_operation op;

      op.payment_service_provider_account = get_account(payment_service_provider_account).id;
      op.auth_key = auth_key;
      op.account = get_account(user_account).id;
      op.debit_amount = get_asset(asset_symbol).amount_from_string(asset_amount);
      op.clearing_account = get_account(clearing_account).id;
      op.transaction_id = transaction_id;
      op.details = details;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (payment_service_provider_account)(auth_key)(user_account)(asset_amount)(asset_symbol)(clearing_account)(transaction_id)(details)(broadcast) ) }

   signed_transaction daspay_credit_account(const string& payment_service_provider_account,
                                            const string& user_account,
                                            const string& asset_amount,
                                            const string& asset_symbol,
                                            const string& clearing_account,
                                            const string& transaction_id,
                                            optional<string> details,
                                            bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      daspay_credit_account_operation op;

      op.payment_service_provider_account = get_account(payment_service_provider_account).id;
      op.account = get_account(user_account).id;
      op.credit_amount = get_asset(asset_symbol).amount_from_string(asset_amount);
      op.clearing_account = get_account(clearing_account).id;
      op.transaction_id = transaction_id;
      op.details = details;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (payment_service_provider_account)(user_account)(asset_amount)(asset_symbol)(clearing_account)(transaction_id)(details)(broadcast) ) }

   signed_transaction update_daspay_clearing_parameters(const string& authority,
                                                        optional<bool> clearing_enabled,
                                                        optional<uint32_t> clearing_interval_time_seconds,
                                                        optional<share_type> collateral_dascoin,
                                                        optional<share_type> collateral_webeur,
                                                        bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      update_daspay_clearing_parameters_operation op;

      op.authority = get_account(authority).id;
      op.clearing_enabled = clearing_enabled;
      op.clearing_interval_time_seconds = clearing_interval_time_seconds;
      op.collateral_dascoin = collateral_dascoin;
      op.collateral_webeur = collateral_webeur;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (clearing_enabled)(clearing_interval_time_seconds)(collateral_dascoin)(collateral_webeur)(broadcast) ) }

   signed_transaction daspay_set_use_external_token_price(const string& authority,
                                                          flat_set<asset_id_type> use_external_token_price,
                                                          bool broadcast = false)
   { try {
       FC_ASSERT( !self.is_locked() );

       daspay_set_use_external_token_price_operation op;

       op.authority = get_account(authority).id;
       op.use_external_token_price = use_external_token_price;

       signed_transaction tx;
       tx.operations.push_back(op);
       set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
       tx.validate();

       return sign_transaction(tx, broadcast);
    } FC_CAPTURE_AND_RETHROW( (authority)(use_external_token_price)(broadcast) ) }

   signed_transaction create_das33_project(const string& authority,
                                           const string& name,
                                           const string& owner,
                                           const string& token,
                                           const vector<pair<string, share_type>>& discounts,
                                           share_type goal_amount,
                                           share_type min_pledge,
                                           share_type max_pledge,
                                           bool broadcast)
   { try {
         FC_ASSERT( !self.is_locked() );

         das33_project_create_operation op;

         op.authority = get_account(authority).id;
         op.name = name;
         op.owner = get_account(owner).id;
         op.token = get_asset_id(token);
         op.goal_amount_eur = goal_amount;
         op.min_pledge = min_pledge;
         op.max_pledge = max_pledge;

         //FC_ASSERT(ratios.size() % 2 == 0, "Must have even number of items in bonuses");
         map<asset_id_type, share_type> discount_map;
         for(uint32_t i=0; i<discounts.size(); i++)
         {
             //bonus_map.emplace_back(get_asset(ratios[i].second).amount_from_string(ratios[i].first), get_asset(ratios[i+1].second).amount_from_string(ratios[i+1].first));
             discount_map[get_asset_id(discounts[i].first)] = discounts[i].second;
         }
         op.discounts = discount_map;

         signed_transaction tx;
         tx.operations.push_back(op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (authority)(name)(owner)(token)(discounts)(goal_amount)(broadcast) ) }

   signed_transaction update_das33_project(const string& authority,
                                           const string& project_id,
                                           optional<string> name,
                                           optional<string> owner,
                                           optional<share_type> goal_amount,
                                           optional<price> token_price,
                                           optional<vector<pair<string, share_type>>> discounts,
                                           optional<share_type> min_pledge,
                                           optional<share_type> max_pledge,
                                           optional<share_type> phase_limit,
                                           optional<time_point_sec> phase_end,
                                           optional<uint8_t> status,
                                           bool broadcast)
   { try {
         FC_ASSERT( !self.is_locked() );

         das33_project_update_operation op;

         op.authority = get_account(authority).id;
         auto id = maybe_id<das33_project_id_type>(project_id);
         op.project_id = *id;
         if (name) op.name = name;
         if (owner) op.owner = get_account(*owner).id;
         if (goal_amount) op.goal_amount = *goal_amount;
         if (token_price) op.token_price = *token_price;
         if (min_pledge) op.min_pledge = *min_pledge;
         if (max_pledge) op.max_pledge = *max_pledge;
         if (phase_limit) op.phase_limit = *phase_limit;
         if (phase_end) op.phase_end = *phase_end;
         if (status) op.status = *status;

         if (discounts)
         {
           map<asset_id_type, share_type> bonus_map;
           for(uint32_t i=0; i<(*discounts).size(); i++)
           {
             bonus_map[get_asset_id((*discounts)[i].first)] = (*discounts)[i].second;
           }
           op.discounts = bonus_map;
         }

         signed_transaction tx;
         tx.operations.push_back(op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (authority)(project_id)(name)(owner)(discounts)(token_price)(status)(broadcast) ) }

   signed_transaction delete_das33_project(const string& authority,
                                           const string& project_id,
                                           bool broadcast)
   { try {
         FC_ASSERT( !self.is_locked() );

         das33_project_delete_operation op;

         op.authority = get_account(authority).id;
         auto id = maybe_id<das33_project_id_type>(project_id);
         op.project_id = *id;

         signed_transaction tx;
         tx.operations.push_back(op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (authority)(project_id)(broadcast) ) }

   signed_transaction das33_pledge_asset(const string& account,
                                         const string& amount,
                                         const string& symbol,
                                         optional<license_type_id_type> license,
                                         das33_project_id_type project,
                                         bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      das33_pledge_asset_operation op;

      op.account_id = get_account(account).id;
      op.pledged = get_asset(symbol).amount_from_string(amount);
      op.license_id = license;
      op.project_id = project;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account)(amount)(license)(project)(broadcast) ) }

   signed_transaction das33_pledge_reject(const string& authority,
                                          const string& pledge_id,
                                          bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      das33_pledge_reject_operation op;

      op.authority = get_account(authority).id;
      auto id = maybe_id<das33_pledge_holder_id_type>(pledge_id);
      op.pledge =  *id;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(pledge_id)(broadcast) ) }

   signed_transaction das33_distribute_pledge(const string& authority,
                                              const string& pledge_id,
                                              share_type to_escrow,
                                              share_type base_to_pledger,
                                              share_type bonus_to_pledger,
                                              bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      das33_distribute_pledge_operation op;

      op.authority = get_account(authority).id;
      auto id = maybe_id<das33_pledge_holder_id_type>(pledge_id);
      op.pledge =  *id;
      op.to_escrow = to_escrow;
      op.bonus_to_pledger = bonus_to_pledger;
      op.base_to_pledger = base_to_pledger;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(pledge_id)(to_escrow)(base_to_pledger)(bonus_to_pledger)(broadcast) ) }

   signed_transaction das33_project_reject(const string& authority,
                                           const string& project_id,
                                           bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      das33_project_reject_operation op;

      op.authority = get_account(authority).id;
      auto id = maybe_id<das33_project_id_type>(project_id);
      op.project =  *id;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(project_id)(broadcast) ) }

   signed_transaction das33_distribute_project_pledges(const string& authority,
                                                       const string& project_id,
                                                       optional<share_type> phase_number,
                                                       share_type to_escrow,
                                                       share_type base_to_pledger,
                                                       share_type bonus_to_pledger,
                                                       bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      das33_distribute_project_pledges_operation op;

      op.authority = get_account(authority).id;
      auto id = maybe_id<das33_project_id_type>(project_id);
      op.project =  *id;
      op.phase_number = phase_number;
      op.to_escrow = to_escrow;
      op.bonus_to_pledger = bonus_to_pledger;
      op.base_to_pledger = base_to_pledger;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(project_id)(phase_number)(to_escrow)(base_to_pledger)(bonus_to_pledger)(broadcast) ) }

   signed_transaction das33_set_use_external_btc_price(const string& authority,
                                                       bool use_exteranl_btc_price,
                                                       bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      das33_set_use_external_btc_price_operation op;

      op.authority = get_account(authority).id;
      op.use_external_btc_price = use_exteranl_btc_price;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(use_exteranl_btc_price)(broadcast) ) }

   signed_transaction das33_set_use_market_token_price(const string& authority,
                                                       flat_set<asset_id_type> use_market_price_for_token,
                                                       bool broadcast = false)
   { try {
       FC_ASSERT( !self.is_locked() );

       das33_set_use_market_price_for_token_operation op;

       op.authority = get_account(authority).id;
       op.use_market_price_for_token = use_market_price_for_token;

       signed_transaction tx;
       tx.operations.push_back(op);
       set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
       tx.validate();

       return sign_transaction(tx, broadcast);
    } FC_CAPTURE_AND_RETHROW( (authority)(use_market_price_for_token)(broadcast) ) }

   signed_transaction update_delayed_operations_resolver_parameters(const string& authority, optional<bool> delayed_operations_resolver_enabled,
                                                        optional<uint32_t> delayed_operations_resolver_interval_time_seconds,
                                                        bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      update_delayed_operations_resolver_parameters_operation op;

      op.authority = get_account(authority).id;
      op.delayed_operations_resolver_enabled = delayed_operations_resolver_enabled;
      op.delayed_operations_resolver_interval_time_seconds = delayed_operations_resolver_interval_time_seconds;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (delayed_operations_resolver_enabled)(delayed_operations_resolver_interval_time_seconds)(broadcast) ) }

   signed_transaction update_global_parameters(const string& authority,
                                               const variant_object& changed_values,
                                               bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );
      FC_ASSERT( !changed_values.contains("current_fees") );

      const chain_parameters& current_params = get_global_properties().parameters;
      chain_parameters new_params = current_params;
      fc::reflector<chain_parameters>::visit(
         fc::from_variant_visitor<chain_parameters>( changed_values, new_params, GRAPHENE_MAX_NESTED_OBJECTS )
      );

      update_global_parameters_operation op;

      op.authority = get_account(authority).id;
      op.new_parameters = new_params;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(changed_values)(broadcast) ) }

   signed_transaction change_operation_fee(const string& authority,
                                           share_type new_fee,
                                           unsigned op_num,
                                           string comment,
                                           bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      change_operation_fee_operation op;

      op.issuer = get_account(authority).id;
      op.new_fee = new_fee.value;
      op.op_num = op_num;
      op.comment = comment;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (authority)(new_fee)(op_num)(comment)(broadcast) ) }

   signed_transaction update_external_btc_price(const string& btc_issuer,
                                                price new_price,
                                                bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      update_external_btc_price_operation op;

      op.issuer = get_account(btc_issuer).id;
      op.eur_amount_per_btc = new_price;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (btc_issuer)(new_price)(broadcast) ) }

   signed_transaction update_external_token_price(const string& token_issuer,
                                                  asset_id_type token_id,
                                                  price new_price,
                                                  bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      update_external_token_price_operation op;

      op.issuer = get_account(token_issuer).id;
      op.token_id = token_id;
      op.eur_amount_per_token = new_price;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (token_issuer)(token_id)(new_price)(broadcast) ) }

   signed_transaction set_active_authorities(const string& account_id_or_name,
                                             vector<public_key_type> keys,
                                             bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_update_operation op;
      authority auth;

      weight_type i = 1;
      for (const auto& key : keys)
          auth.add_authority(key, i++);

      op.account = get_account(account_id_or_name).id;
      op.active = auth;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account_id_or_name)(keys)(broadcast) ) }

   signed_transaction tether_accounts(string wallet, string vault, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_object wallet_account = get_account(wallet);
      account_object vault_account = get_account(vault);
      account_id_type wallet_id = wallet_account.id;
      account_id_type vault_id = vault_account.id;

      tether_accounts_operation tether_op;

      tether_op.wallet_account = wallet_id;
      tether_op.vault_account = vault_id;

      signed_transaction tx;
      tx.operations.push_back(tether_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (wallet)(vault)(broadcast) ) }

   signed_transaction transfer_cycles_from_licence_to_wallet(string vault, license_type_id_type license, share_type amount_of_cycles_to_transfer, string wallet, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_object vault_account = get_account(vault);
      account_object wallet_account = get_account(wallet);

      transfer_cycles_from_licence_to_wallet_operation transfer_op;

      transfer_op.vault_id = vault_account.id;
      transfer_op.wallet_id = wallet_account.id;
      transfer_op.license_id = license;
      transfer_op.amount = amount_of_cycles_to_transfer;

      signed_transaction tx;
      tx.operations.push_back(transfer_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (vault)(license)(amount_of_cycles_to_transfer)(wallet)(broadcast) ) }

   signed_transaction purchase_cycle_asset(string account, string amount_to_sell, string symbol_to_sell, double frequency, double amount_of_cycles_to_receive, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );

      account_object buyer_account = get_account(account);

      purchase_cycle_asset_operation purchase_op;

      purchase_op.wallet_id = buyer_account.id;
      purchase_op.amount = get_asset(symbol_to_sell).amount_from_string(amount_to_sell);
      purchase_op.frequency = static_cast<frequency_type>(frequency);
      purchase_op.expected_amount = amount_of_cycles_to_receive;

      signed_transaction tx;
      tx.operations.push_back(purchase_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (amount_to_sell)(symbol_to_sell)(broadcast)(amount_of_cycles_to_receive) ) }

   signed_transaction upgrade_account(string name, bool broadcast)
   { try {
      FC_ASSERT( !self.is_locked() );
      account_object account_obj = get_account(name);
      FC_ASSERT( !account_obj.is_lifetime_member() );

      signed_transaction tx;
      account_upgrade_operation op;
      op.account_to_upgrade = account_obj.get_id();
      op.upgrade_to_lifetime_member = true;
      tx.operations = {op};
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (name) ) }


   // This function generates derived keys starting with index 0 and keeps incrementing
   // the index until it finds a key that isn't registered in the block chain.  To be
   // safer, it continues checking for a few more keys to make sure there wasn't a short gap
   // caused by a failed registration or the like.
   int find_first_unused_derived_key_index(const fc::ecc::private_key& parent_key)
   {
      int first_unused_index = 0;
      int number_of_consecutive_unused_keys = 0;
      for (int key_index = 0; ; ++key_index)
      {
         fc::ecc::private_key derived_private_key = derive_private_key(key_to_wif(parent_key), key_index);
         graphene::chain::public_key_type derived_public_key = derived_private_key.get_public_key();
         if( _keys.find(derived_public_key) == _keys.end() )
         {
            if (number_of_consecutive_unused_keys)
            {
               ++number_of_consecutive_unused_keys;
               if (number_of_consecutive_unused_keys > 5)
                  return first_unused_index;
            }
            else
            {
               first_unused_index = key_index;
               number_of_consecutive_unused_keys = 1;
            }
         }
         else
         {
            // key_index is used
            first_unused_index = 0;
            number_of_consecutive_unused_keys = 0;
         }
      }
   }

   signed_transaction create_account_with_private_key(fc::ecc::private_key owner_privkey,
                                                      string account_name,
                                                      bool broadcast = false,
                                                      bool save_wallet = true)
   { try {
         int active_key_index = find_first_unused_derived_key_index(owner_privkey);
         fc::ecc::private_key active_privkey = derive_private_key( key_to_wif(owner_privkey), active_key_index);

         int memo_key_index = find_first_unused_derived_key_index(active_privkey);
         fc::ecc::private_key memo_privkey = derive_private_key( key_to_wif(active_privkey), memo_key_index);

         graphene::chain::public_key_type owner_pubkey = owner_privkey.get_public_key();
         graphene::chain::public_key_type active_pubkey = active_privkey.get_public_key();
         graphene::chain::public_key_type memo_pubkey = memo_privkey.get_public_key();

         account_create_operation account_create_op;

         // TODO:  process when pay_from_account is ID

         // TODO: this value is hardcoded, do this in a more elegant manner.
         account_object registrar_account_object = get_account("sys.registrar");

         account_id_type registrar_account_id = registrar_account_object.id;

         // TODO: referrer is set to NULL, it is pending to be removed.
         account_object referrer_account_object = get_account( GRAPHENE_NULL_ACCOUNT );
         account_create_op.referrer = referrer_account_object.id;
         account_create_op.referrer_percent = referrer_account_object.referrer_rewards_percentage;

         account_create_op.registrar = registrar_account_id;
         account_create_op.name = account_name;
         account_create_op.owner = authority(1, owner_pubkey, 1);
         account_create_op.active = authority(1, active_pubkey, 1);
         account_create_op.options.memo_key = memo_pubkey;

         // current_fee_schedule()
         // find_account(pay_from_account)

         // account_create_op.fee = account_create_op.calculate_fee(db.current_fee_schedule());

         signed_transaction tx;

         tx.operations.push_back( account_create_op );

         set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);

         vector<public_key_type> paying_keys = registrar_account_object.active.get_keys();

         auto dyn_props = get_dynamic_global_properties();
         tx.set_reference_block( dyn_props.head_block_id );
         tx.set_expiration( dyn_props.time + fc::seconds(30) );
         tx.validate();

         for( public_key_type& key : paying_keys )
         {
            auto it = _keys.find(key);
            if( it != _keys.end() )
            {
               fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
               FC_ASSERT( privkey.valid(), "Malformed private key in _keys" );
               tx.sign( *privkey, _chain_id );
            }
         }

         // we do not insert owner_privkey here because
         //    it is intended to only be used for key recovery
         _wallet.pending_account_registrations[account_name].push_back(key_to_wif( active_privkey ));
         _wallet.pending_account_registrations[account_name].push_back(key_to_wif( memo_privkey ));
         if( save_wallet )
            save_wallet_file();
         if( broadcast )
            _remote_net_broadcast->broadcast_transaction( tx );
         return tx;
   } FC_CAPTURE_AND_RETHROW( (account_name)(broadcast) ) }

   signed_transaction create_account_with_brain_key(string brain_key,
                                                    string account_name,
                                                    bool broadcast = false,
                                                    bool save_wallet = true)
   { try {
      FC_ASSERT( !self.is_locked() );
      string normalized_brain_key = normalize_brain_key( brain_key );
      // TODO:  scan blockchain for accounts that exist with same brain key
      fc::ecc::private_key owner_privkey = derive_private_key( normalized_brain_key, 0 );
      return create_account_with_private_key( owner_privkey, account_name, broadcast, save_wallet );
   } FC_CAPTURE_AND_RETHROW( (account_name) ) }


   signed_transaction create_asset(string issuer,
                                   string symbol,
                                   uint8_t precision,
                                   asset_options common,
                                   fc::optional<bitasset_options> bitasset_opts,
                                   bool broadcast = false)
   { try {
      account_object issuer_account = get_account( issuer );
      FC_ASSERT(!find_asset(symbol).valid(), "Asset with that symbol already exists!");

      asset_create_operation create_op;
      create_op.issuer = issuer_account.id;
      create_op.symbol = symbol;
      create_op.precision = precision;
      create_op.common_options = common;
      create_op.bitasset_opts = bitasset_opts;

      signed_transaction tx;
      tx.operations.push_back( create_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (issuer)(symbol)(precision)(common)(bitasset_opts)(broadcast) ) }

   signed_transaction update_asset(string symbol,
                                   optional<string> new_issuer,
                                   asset_options new_options,
                                   bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");
      optional<account_id_type> new_issuer_account_id;
      if (new_issuer)
      {
        account_object new_issuer_account = get_account(*new_issuer);
        new_issuer_account_id = new_issuer_account.id;
      }

      asset_update_operation update_op;
      update_op.issuer = asset_to_update->issuer;
      update_op.asset_to_update = asset_to_update->id;
      update_op.new_issuer = new_issuer_account_id;
      update_op.new_options = new_options;

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_issuer)(new_options)(broadcast) ) }

   signed_transaction update_bitasset(string symbol,
                                      bitasset_options new_options,
                                      bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      asset_update_bitasset_operation update_op;
      update_op.issuer = asset_to_update->issuer;
      update_op.asset_to_update = asset_to_update->id;
      update_op.new_options = new_options;

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_options)(broadcast) ) }

   signed_transaction update_asset_feed_producers(string symbol,
                                                  flat_set<string> new_feed_producers,
                                                  bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      asset_update_feed_producers_operation update_op;
      update_op.issuer = asset_to_update->issuer;
      update_op.asset_to_update = asset_to_update->id;
      update_op.new_feed_producers.reserve(new_feed_producers.size());
      std::transform(new_feed_producers.begin(), new_feed_producers.end(),
                     std::inserter(update_op.new_feed_producers, update_op.new_feed_producers.end()),
                     [this](const std::string& account_name_or_id){ return get_account_id(account_name_or_id); });

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(new_feed_producers)(broadcast) ) }

   signed_transaction publish_asset_feed(string publishing_account,
                                         string symbol,
                                         price_feed feed,
                                         bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_update = find_asset(symbol);
      if (!asset_to_update)
        FC_THROW("No asset with that symbol exists!");

      asset_publish_feed_operation publish_op;
      publish_op.publisher = get_account_id(publishing_account);
      publish_op.asset_id = asset_to_update->id;
      publish_op.feed = feed;

      signed_transaction tx;
      tx.operations.push_back( publish_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (publishing_account)(symbol)(feed)(broadcast) ) }

   signed_transaction fund_asset_fee_pool(string from,
                                          string symbol,
                                          string amount,
                                          bool broadcast /* = false */)
   { try {
      account_object from_account = get_account(from);
      optional<asset_object> asset_to_fund = find_asset(symbol);
      if (!asset_to_fund)
        FC_THROW("No asset with that symbol exists!");
      asset_object core_asset = get_asset(asset_id_type());

      asset_fund_fee_pool_operation fund_op;
      fund_op.from_account = from_account.id;
      fund_op.asset_id = asset_to_fund->id;
      fund_op.amount = core_asset.amount_from_string(amount).amount;

      signed_transaction tx;
      tx.operations.push_back( fund_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (from)(symbol)(amount)(broadcast) ) }

   signed_transaction claim_asset_accumulated_fees_pool(string symbol,
                                                        string amount,
                                                        bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_pool_to_claim = find_asset(symbol);
      if (!asset_pool_to_claim)
        FC_THROW("No asset with that symbol exists!");

      asset_claim_fees_operation claim_op;
      claim_op.issuer = asset_pool_to_claim->issuer;
      claim_op.amount_to_claim = asset_pool_to_claim->amount_from_string(amount);

      signed_transaction tx;
      tx.operations.push_back( claim_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(amount)(broadcast) ) }

   signed_transaction reserve_asset(string from,
                                 string amount,
                                 string symbol,
                                 bool broadcast /* = false */)
   { try {
      account_object from_account = get_account(from);
      optional<asset_object> asset_to_reserve = find_asset(symbol);
      if (!asset_to_reserve)
        FC_THROW("No asset with that symbol exists!");

      asset_reserve_operation reserve_op;
      reserve_op.payer = from_account.id;
      reserve_op.amount_to_reserve = asset_to_reserve->amount_from_string(amount);

      signed_transaction tx;
      tx.operations.push_back( reserve_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (from)(amount)(symbol)(broadcast) ) }

   signed_transaction global_settle_asset(string symbol,
                                          price settle_price,
                                          bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_settle = find_asset(symbol);
      if (!asset_to_settle)
        FC_THROW("No asset with that symbol exists!");

      asset_global_settle_operation settle_op;
      settle_op.issuer = asset_to_settle->issuer;
      settle_op.asset_to_settle = asset_to_settle->id;
      settle_op.settle_price = settle_price;

      signed_transaction tx;
      tx.operations.push_back( settle_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (symbol)(settle_price)(broadcast) ) }

   signed_transaction settle_asset(string account_to_settle,
                                   string amount_to_settle,
                                   string symbol,
                                   bool broadcast /* = false */)
   { try {
      optional<asset_object> asset_to_settle = find_asset(symbol);
      if (!asset_to_settle)
        FC_THROW("No asset with that symbol exists!");

      asset_settle_operation settle_op;
      settle_op.account = get_account_id(account_to_settle);
      settle_op.amount = asset_to_settle->amount_from_string(amount_to_settle);

      signed_transaction tx;
      tx.operations.push_back( settle_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_to_settle)(amount_to_settle)(symbol)(broadcast) ) }

   signed_transaction whitelist_account(string authorizing_account,
                                        string account_to_list,
                                        account_whitelist_operation::account_listing new_listing_status,
                                        bool broadcast /* = false */)
   { try {
      account_whitelist_operation whitelist_op;
      whitelist_op.authorizing_account = get_account_id(authorizing_account);
      whitelist_op.account_to_list = get_account_id(account_to_list);
      whitelist_op.new_listing = new_listing_status;

      signed_transaction tx;
      tx.operations.push_back( whitelist_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (authorizing_account)(account_to_list)(new_listing_status)(broadcast) ) }

   signed_transaction create_committee_member(string owner_account, string url,
                                      bool broadcast /* = false */)
   { try {

      committee_member_create_operation committee_member_create_op;
      committee_member_create_op.committee_member_account = get_account_id(owner_account);
      committee_member_create_op.url = url;
      if (_remote_db->get_committee_member_by_account(committee_member_create_op.committee_member_account))
         FC_THROW("Account ${owner_account} is already a committee_member", ("owner_account", owner_account));

      signed_transaction tx;
      tx.operations.push_back( committee_member_create_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (owner_account)(broadcast) ) }

   witness_object get_witness(string owner_account)
   {
      try
      {
         fc::optional<witness_id_type> witness_id = maybe_id<witness_id_type>(owner_account);
         if (witness_id)
         {
            std::vector<witness_id_type> ids_to_get;
            ids_to_get.push_back(*witness_id);
            std::vector<fc::optional<witness_object>> witness_objects = _remote_db->get_witnesses(ids_to_get);
            if (witness_objects.front())
               return *witness_objects.front();
            FC_THROW("No witness is registered for id ${id}", ("id", owner_account));
         }
         else
         {
            // then maybe it's the owner account
            try
            {
               account_id_type owner_account_id = get_account_id(owner_account);
               fc::optional<witness_object> witness = _remote_db->get_witness_by_account(owner_account_id);
               if (witness)
                  return *witness;
               else
                  FC_THROW("No witness is registered for account ${account}", ("account", owner_account));
            }
            catch (const fc::exception&)
            {
               FC_THROW("No account or witness named ${account}", ("account", owner_account));
            }
         }
      }
      FC_CAPTURE_AND_RETHROW( (owner_account) )
   }

   committee_member_object get_committee_member(string owner_account)
   {
      try
      {
         fc::optional<committee_member_id_type> committee_member_id = maybe_id<committee_member_id_type>(owner_account);
         if (committee_member_id)
         {
            std::vector<committee_member_id_type> ids_to_get;
            ids_to_get.push_back(*committee_member_id);
            std::vector<fc::optional<committee_member_object>> committee_member_objects = _remote_db->get_committee_members(ids_to_get);
            if (committee_member_objects.front())
               return *committee_member_objects.front();
            FC_THROW("No committee_member is registered for id ${id}", ("id", owner_account));
         }
         else
         {
            // then maybe it's the owner account
            try
            {
               account_id_type owner_account_id = get_account_id(owner_account);
               fc::optional<committee_member_object> committee_member = _remote_db->get_committee_member_by_account(owner_account_id);
               if (committee_member)
                  return *committee_member;
               else
                  FC_THROW("No committee_member is registered for account ${account}", ("account", owner_account));
            }
            catch (const fc::exception&)
            {
               FC_THROW("No account or committee_member named ${account}", ("account", owner_account));
            }
         }
      }
      FC_CAPTURE_AND_RETHROW( (owner_account) )
   }

   signed_transaction create_witness(string owner_account,
                                     string url,
                                     bool broadcast /* = false */)
   { try {
      account_object witness_account = get_account(owner_account);
      fc::ecc::private_key active_private_key = get_private_key_for_account(witness_account);
      int witness_key_index = find_first_unused_derived_key_index(active_private_key);
      fc::ecc::private_key witness_private_key = derive_private_key(key_to_wif(active_private_key), witness_key_index);
      graphene::chain::public_key_type witness_public_key = witness_private_key.get_public_key();

      witness_create_operation witness_create_op;
      witness_create_op.witness_account = witness_account.id;
      witness_create_op.block_signing_key = witness_public_key;
      witness_create_op.url = url;

      if (_remote_db->get_witness_by_account(witness_create_op.witness_account))
         FC_THROW("Account ${owner_account} is already a witness", ("owner_account", owner_account));

      signed_transaction tx;
      tx.operations.push_back( witness_create_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      _wallet.pending_witness_registrations[owner_account] = key_to_wif(witness_private_key);

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (owner_account)(broadcast) ) }

   signed_transaction update_witness(string witness_name,
                                     string url,
                                     string block_signing_key,
                                     bool broadcast /* = false */)
   { try {
      witness_object witness = get_witness(witness_name);
      account_object witness_account = get_account( witness.witness_account );
      fc::ecc::private_key active_private_key = get_private_key_for_account(witness_account);

      witness_update_operation witness_update_op;
      witness_update_op.witness = witness.id;
      witness_update_op.witness_account = witness_account.id;
      if( url != "" )
         witness_update_op.new_url = url;
      if( block_signing_key != "" )
         witness_update_op.new_signing_key = public_key_type( block_signing_key );

      signed_transaction tx;
      tx.operations.push_back( witness_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (witness_name)(url)(block_signing_key)(broadcast) ) }

   template<typename WorkerInit>
   static WorkerInit _create_worker_initializer( const variant& worker_settings )
   {
      WorkerInit result;
      from_variant( worker_settings, result, GRAPHENE_MAX_NESTED_OBJECTS );
      return result;
   }

   signed_transaction create_worker(
      string owner_account,
      time_point_sec work_begin_date,
      time_point_sec work_end_date,
      share_type daily_pay,
      string name,
      string url,
      variant worker_settings,
      bool broadcast
      )
   {
      worker_initializer init;
      std::string wtype = worker_settings["type"].get_string();

      // TODO:  Use introspection to do this dispatch
      if( wtype == "burn" )
         init = _create_worker_initializer< burn_worker_initializer >( worker_settings );
      else if( wtype == "refund" )
         init = _create_worker_initializer< refund_worker_initializer >( worker_settings );
      else if( wtype == "vesting" )
         init = _create_worker_initializer< vesting_balance_worker_initializer >( worker_settings );
      else
      {
         FC_ASSERT( false, "unknown worker[\"type\"] value" );
      }

      worker_create_operation op;
      op.owner = get_account( owner_account ).id;
      op.work_begin_date = work_begin_date;
      op.work_end_date = work_end_date;
      op.daily_pay = daily_pay;
      op.name = name;
      op.url = url;
      op.initializer = init;

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction update_worker_votes(
      string account,
      worker_vote_delta delta,
      bool broadcast
      )
   {
      account_object acct = get_account( account );
      account_update_operation op;

      // you could probably use a faster algorithm for this, but flat_set is fast enough :)
      flat_set< worker_id_type > merged;
      merged.reserve( delta.vote_for.size() + delta.vote_against.size() + delta.vote_abstain.size() );
      for( const worker_id_type& wid : delta.vote_for )
      {
         bool inserted = merged.insert( wid ).second;
         FC_ASSERT( inserted, "worker ${wid} specified multiple times", ("wid", wid) );
      }
      for( const worker_id_type& wid : delta.vote_against )
      {
         bool inserted = merged.insert( wid ).second;
         FC_ASSERT( inserted, "worker ${wid} specified multiple times", ("wid", wid) );
      }
      for( const worker_id_type& wid : delta.vote_abstain )
      {
         bool inserted = merged.insert( wid ).second;
         FC_ASSERT( inserted, "worker ${wid} specified multiple times", ("wid", wid) );
      }

      // should be enforced by FC_ASSERT's above
      assert( merged.size() == delta.vote_for.size() + delta.vote_against.size() + delta.vote_abstain.size() );

      vector< object_id_type > query_ids;
      for( const worker_id_type& wid : merged )
         query_ids.push_back( wid );

      flat_set<vote_id_type> new_votes( acct.options.votes );

      fc::variants objects = _remote_db->get_objects( query_ids );
      for( const variant& obj : objects )
      {
         worker_object wo;
         from_variant( obj, wo, GRAPHENE_MAX_NESTED_OBJECTS );
         new_votes.erase( wo.vote_for );
         new_votes.erase( wo.vote_against );
         if( delta.vote_for.find( wo.id ) != delta.vote_for.end() )
            new_votes.insert( wo.vote_for );
         else if( delta.vote_against.find( wo.id ) != delta.vote_against.end() )
            new_votes.insert( wo.vote_against );
         else
            assert( delta.vote_abstain.find( wo.id ) != delta.vote_abstain.end() );
      }

      account_update_operation update_op;
      update_op.account = acct.id;
      update_op.new_options = acct.options;
      update_op.new_options->votes = new_votes;

      signed_transaction tx;
      tx.operations.push_back( update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   vector< vesting_balance_object_with_info > get_vesting_balances( string account_name )
   { try {
      fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>( account_name );
      std::vector<vesting_balance_object_with_info> result;
      fc::time_point_sec now = _remote_db->get_dynamic_global_properties().time;

      if( vbid )
      {
         result.emplace_back( get_object<vesting_balance_object>(*vbid), now );
         return result;
      }

      // try casting to avoid a round-trip if we were given an account ID
      fc::optional<account_id_type> acct_id = maybe_id<account_id_type>( account_name );
      if( !acct_id )
         acct_id = get_account( account_name ).id;

      vector< vesting_balance_object > vbos = _remote_db->get_vesting_balances( *acct_id );
      if( vbos.size() == 0 )
         return result;

      for( const vesting_balance_object& vbo : vbos )
         result.emplace_back( vbo, now );

      return result;
   } FC_CAPTURE_AND_RETHROW( (account_name) )
   }

   signed_transaction withdraw_vesting(
      string witness_name,
      string amount,
      string asset_symbol,
      bool broadcast = false )
   { try {
      asset_object asset_obj = get_asset( asset_symbol );
      fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>(witness_name);
      if( !vbid )
      {
         witness_object wit = get_witness( witness_name );
         FC_ASSERT( wit.pay_vb );
         vbid = wit.pay_vb;
      }

      vesting_balance_object vbo = get_object< vesting_balance_object >( *vbid );
      vesting_balance_withdraw_operation vesting_balance_withdraw_op;

      vesting_balance_withdraw_op.vesting_balance = *vbid;
      vesting_balance_withdraw_op.owner = vbo.owner;
      vesting_balance_withdraw_op.amount = asset_obj.amount_from_string(amount);

      signed_transaction tx;
      tx.operations.push_back( vesting_balance_withdraw_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (witness_name)(amount) )
   }

   signed_transaction vote_for_committee_member(string voting_account,
                                        string committee_member,
                                        bool approve,
                                        bool broadcast /* = false */)
   { try {
      account_object voting_account_object = get_account(voting_account);
      account_id_type committee_member_owner_account_id = get_account_id(committee_member);
      fc::optional<committee_member_object> committee_member_obj = _remote_db->get_committee_member_by_account(committee_member_owner_account_id);
      if (!committee_member_obj)
         FC_THROW("Account ${committee_member} is not registered as a committee_member", ("committee_member", committee_member));
      if (approve)
      {
         auto insert_result = voting_account_object.options.votes.insert(committee_member_obj->vote_id);
         if (!insert_result.second)
            FC_THROW("Account ${account} was already voting for committee_member ${committee_member}", ("account", voting_account)("committee_member", committee_member));
      }
      else
      {
         unsigned votes_removed = voting_account_object.options.votes.erase(committee_member_obj->vote_id);
         if (!votes_removed)
            FC_THROW("Account ${account} is already not voting for committee_member ${committee_member}", ("account", voting_account)("committee_member", committee_member));
      }
      account_update_operation account_update_op;
      account_update_op.account = voting_account_object.id;
      account_update_op.new_options = voting_account_object.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (voting_account)(committee_member)(approve)(broadcast) ) }

   signed_transaction vote_for_witness(string voting_account,
                                        string witness,
                                        bool approve,
                                        bool broadcast /* = false */)
   { try {
      account_object voting_account_object = get_account(voting_account);
      account_id_type witness_owner_account_id = get_account_id(witness);
      fc::optional<witness_object> witness_obj = _remote_db->get_witness_by_account(witness_owner_account_id);
      if (!witness_obj)
         FC_THROW("Account ${witness} is not registered as a witness", ("witness", witness));
      if (approve)
      {
         auto insert_result = voting_account_object.options.votes.insert(witness_obj->vote_id);
         if (!insert_result.second)
            FC_THROW("Account ${account} was already voting for witness ${witness}", ("account", voting_account)("witness", witness));
      }
      else
      {
         unsigned votes_removed = voting_account_object.options.votes.erase(witness_obj->vote_id);
         if (!votes_removed)
            FC_THROW("Account ${account} is already not voting for witness ${witness}", ("account", voting_account)("witness", witness));
      }
      account_update_operation account_update_op;
      account_update_op.account = voting_account_object.id;
      account_update_op.new_options = voting_account_object.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (voting_account)(witness)(approve)(broadcast) ) }

   signed_transaction set_voting_proxy(string account_to_modify,
                                       optional<string> voting_account,
                                       bool broadcast /* = false */)
   { try {
      account_object account_object_to_modify = get_account(account_to_modify);
      if (voting_account)
      {
         account_id_type new_voting_account_id = get_account_id(*voting_account);
         if (account_object_to_modify.options.voting_account == new_voting_account_id)
            FC_THROW("Voting proxy for ${account} is already set to ${voter}", ("account", account_to_modify)("voter", *voting_account));
         account_object_to_modify.options.voting_account = new_voting_account_id;
      }
      else
      {
         if (account_object_to_modify.options.voting_account == GRAPHENE_PROXY_TO_SELF_ACCOUNT)
            FC_THROW("Account ${account} is already voting for itself", ("account", account_to_modify));
         account_object_to_modify.options.voting_account = GRAPHENE_PROXY_TO_SELF_ACCOUNT;
      }

      account_update_operation account_update_op;
      account_update_op.account = account_object_to_modify.id;
      account_update_op.new_options = account_object_to_modify.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_to_modify)(voting_account)(broadcast) ) }

   signed_transaction set_desired_witness_and_committee_member_count(string account_to_modify,
                                                             uint16_t desired_number_of_witnesses,
                                                             uint16_t desired_number_of_committee_members,
                                                             bool broadcast /* = false */)
   { try {
      account_object account_object_to_modify = get_account(account_to_modify);

      if (account_object_to_modify.options.num_witness == desired_number_of_witnesses &&
          account_object_to_modify.options.num_committee == desired_number_of_committee_members)
         FC_THROW("Account ${account} is already voting for ${witnesses} witnesses and ${committee_members} committee_members",
                  ("account", account_to_modify)("witnesses", desired_number_of_witnesses)("committee_members",desired_number_of_witnesses));
      account_object_to_modify.options.num_witness = desired_number_of_witnesses;
      account_object_to_modify.options.num_committee = desired_number_of_committee_members;

      account_update_operation account_update_op;
      account_update_op.account = account_object_to_modify.id;
      account_update_op.new_options = account_object_to_modify.options;

      signed_transaction tx;
      tx.operations.push_back( account_update_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_to_modify)(desired_number_of_witnesses)(desired_number_of_committee_members)(broadcast) ) }

   signed_transaction sign_transaction(signed_transaction tx, bool broadcast = false)
   {
      flat_set<account_id_type> req_active_approvals;
      flat_set<account_id_type> req_owner_approvals;
      vector<authority>         other_auths;

      tx.get_required_authorities( req_active_approvals, req_owner_approvals, other_auths );

      for( const auto& auth : other_auths )
         for( const auto& a : auth.account_auths )
            req_active_approvals.insert(a.first);

      // std::merge lets us de-duplicate account_id's that occur in both
      //   sets, and dump them into a vector (as required by remote_db api)
      //   at the same time
      vector<account_id_type> v_approving_account_ids;
      std::merge(req_active_approvals.begin(), req_active_approvals.end(),
                 req_owner_approvals.begin() , req_owner_approvals.end(),
                 std::back_inserter(v_approving_account_ids));

      /// TODO: fetch the accounts specified via other_auths as well.

      vector< optional<account_object> > approving_account_objects =
            _remote_db->get_accounts( v_approving_account_ids );

      /// TODO: recursively check one layer deeper in the authority tree for keys

      FC_ASSERT( approving_account_objects.size() == v_approving_account_ids.size() );

      flat_map<account_id_type, account_object*> approving_account_lut;
      size_t i = 0;
      for( optional<account_object>& approving_acct : approving_account_objects )
      {
         if( !approving_acct.valid() )
         {
            wlog( "operation_get_required_auths said approval of non-existing account ${id} was needed",
                  ("id", v_approving_account_ids[i]) );
            i++;
            continue;
         }
         approving_account_lut[ approving_acct->id ] = &(*approving_acct);
         i++;
      }

      flat_set<public_key_type> approving_key_set;
      for( account_id_type& acct_id : req_active_approvals )
      {
         const auto it = approving_account_lut.find( acct_id );
         if( it == approving_account_lut.end() )
            continue;
         const account_object* acct = it->second;
         vector<public_key_type> v_approving_keys = acct->active.get_keys();
         for( const public_key_type& approving_key : v_approving_keys )
            approving_key_set.insert( approving_key );
      }
      for( account_id_type& acct_id : req_owner_approvals )
      {
         const auto it = approving_account_lut.find( acct_id );
         if( it == approving_account_lut.end() )
            continue;
         const account_object* acct = it->second;
         vector<public_key_type> v_approving_keys = acct->owner.get_keys();
         for( const public_key_type& approving_key : v_approving_keys )
            approving_key_set.insert( approving_key );
      }
      for( const authority& a : other_auths )
      {
         for( const auto& k : a.key_auths )
            approving_key_set.insert( k.first );
      }

      auto dyn_props = get_dynamic_global_properties();
      tx.set_reference_block( dyn_props.head_block_id );

      // first, some bookkeeping, expire old items from _recently_generated_transactions
      // since transactions include the head block id, we just need the index for keeping transactions unique
      // when there are multiple transactions in the same block.  choose a time period that should be at
      // least one block long, even in the worst case.  2 minutes ought to be plenty.
      fc::time_point_sec oldest_transaction_ids_to_track(dyn_props.time - fc::minutes(2));
      auto oldest_transaction_record_iter = _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_transaction_ids_to_track);
      auto begin_iter = _recently_generated_transactions.get<timestamp_index>().begin();
      _recently_generated_transactions.get<timestamp_index>().erase(begin_iter, oldest_transaction_record_iter);

      uint32_t expiration_time_offset = 0;
      for (;;)
      {
         tx.set_expiration( dyn_props.time + fc::seconds(30 + expiration_time_offset) );
         tx.signatures.clear();

         for( public_key_type& key : approving_key_set )
         {
            auto it = _keys.find(key);
            if( it != _keys.end() )
            {
               fc::optional<fc::ecc::private_key> privkey = wif_to_key( it->second );
               FC_ASSERT( privkey.valid(), "Malformed private key in _keys" );
               tx.sign( *privkey, _chain_id );
            }
            /// TODO: if transaction has enough signatures to be "valid" don't add any more,
            /// there are cases where the wallet may have more keys than strictly necessary and
            /// the transaction will be rejected if the transaction validates without requiring
            /// all signatures provided
         }

         graphene::chain::transaction_id_type this_transaction_id = tx.id();
         auto iter = _recently_generated_transactions.find(this_transaction_id);
         if (iter == _recently_generated_transactions.end())
         {
            // we haven't generated this transaction before, the usual case
            recently_generated_transaction_record this_transaction_record;
            this_transaction_record.generation_time = dyn_props.time;
            this_transaction_record.transaction_id = this_transaction_id;
            _recently_generated_transactions.insert(this_transaction_record);
            break;
         }

         // else we've generated a dupe, increment expiration time and re-sign it
         ++expiration_time_offset;
      }

      if( broadcast )
      {
         try
         {
            _remote_net_broadcast->broadcast_transaction( tx );
         }
         catch (const fc::exception& e)
         {
            elog("Caught exception while broadcasting tx ${id}:  ${e}", ("id", tx.id().str())("e", e.to_detail_string()) );
            throw;
         }
      }

      return tx;
   }

   void expire_recently_generated_transactions(fc::time_point_sec oldest_time)
   {
       // Since transactions include the head block id, we just need the index for keeping transactions unique
       // when there are multiple transactions in the same block.  Choose a time period that should be at
       // least one block long, even in the worst case.
       auto oldest_transaction_record_iter =
           _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_time);
       auto begin_iter = _recently_generated_transactions.get<timestamp_index>().begin();
       _recently_generated_transactions.get<timestamp_index>().erase(begin_iter, oldest_transaction_record_iter);
   }

   void broadcast_signed_transaction(signed_transaction tx)
   {
       try {
           _remote_net_broadcast->broadcast_transaction(tx);
       }
       catch (const fc::exception& e) {
           elog("Caught exception while broadcasting tx ${id}: ${e}",
                ("id", tx.id().str())("e", e.to_detail_string()));
           throw;
       }
   }

   signed_transaction sign_transaction_with_keys(signed_transaction tx, vector<string> wif_keys, bool broadcast)
   {
       if (wif_keys.empty())
           return tx;

       auto dyn_props = get_dynamic_global_properties();
       tx.set_reference_block(dyn_props.head_block_id);

       // Expire all transactions generated in the last few minutes:
       expire_recently_generated_transactions(dyn_props.time - fc::minutes(2));

       uint32_t expiration_time_offset = 0;
       for (;;) {
           tx.set_expiration(dyn_props.time + fc::seconds(30 + expiration_time_offset));
           tx.signatures.clear();

           for (auto wif_key : wif_keys) {
               auto pkey = wif_to_key(wif_key);
               FC_ASSERT(pkey.valid(), "Malformed private key found in key list");
               tx.sign(*pkey, _chain_id);
           }

           auto this_transaction_id = tx.id();
           auto iter = _recently_generated_transactions.find(this_transaction_id);
           if (iter == _recently_generated_transactions.end()) {
               // Transaction is unique.
               _recently_generated_transactions.insert(
                   recently_generated_transaction_record(dyn_props.time, this_transaction_id));
               break;
           }

           // Duplicate transaction has been generated, increment expiration time and re-sign it.
           ++expiration_time_offset;
       }

       if (broadcast)
           broadcast_signed_transaction(tx);

       return tx;
   }

   memo_data sign_memo(string from, string to, string memo)
   {
      FC_ASSERT( !self.is_locked() );

      memo_data md = memo_data();

      // get account memo key, if that fails, try a pubkey
      try {
         account_object from_account = get_account(from);
         md.from = from_account.options.memo_key;
      } catch (const fc::exception& e) {
         md.from =  self.get_public_key( from );
      }
      // same as above, for destination key
      try {
         account_object to_account = get_account(to);
         md.to = to_account.options.memo_key;
      } catch (const fc::exception& e) {
         md.to = self.get_public_key( to );
      }

      md.set_message(get_private_key(md.from), md.to, memo);
      return md;
   }

   string read_memo(const memo_data& md)
   {
      FC_ASSERT(!is_locked());
      std::string clear_text;

      const memo_data *memo = &md;

      try {
         FC_ASSERT(_keys.count(memo->to) || _keys.count(memo->from), "Memo is encrypted to a key ${to} or ${from} not in this wallet.", ("to", memo->to)("from",memo->from));
         if( _keys.count(memo->to) ) {
            auto my_key = wif_to_key(_keys.at(memo->to));
            FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
            clear_text = memo->get_message(*my_key, memo->from);
         } else {
            auto my_key = wif_to_key(_keys.at(memo->from));
            FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
            clear_text = memo->get_message(*my_key, memo->to);
         }
      } catch (const fc::exception& e) {
         elog("Error when decrypting memo: ${e}", ("e", e.to_detail_string()));
      }

      return clear_text;
   }

   signed_transaction sell_asset(string seller_account,
                                 string amount_to_sell,
                                 string symbol_to_sell,
                                 string min_to_receive,
                                 string symbol_to_receive,
                                 uint32_t timeout_sec = 0,
                                 bool   fill_or_kill = false,
                                 bool   broadcast = false)
   {
      account_object seller   = get_account( seller_account );

      limit_order_create_operation op;
      op.seller = seller.id;
      op.amount_to_sell = get_asset(symbol_to_sell).amount_from_string(amount_to_sell);
      op.min_to_receive = get_asset(symbol_to_receive).amount_from_string(min_to_receive);
      if( timeout_sec )
         op.expiration = fc::time_point::now() + fc::seconds(timeout_sec);
      op.fill_or_kill = fill_or_kill;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction borrow_asset(string seller_name, string amount_to_borrow, string asset_symbol,
                                       string amount_of_collateral, bool broadcast = false)
   {
      account_object seller = get_account(seller_name);
      asset_object mia = get_asset(asset_symbol);
      FC_ASSERT(mia.is_market_issued());
      asset_object collateral = get_asset(get_object(*mia.bitasset_data_id).options.short_backing_asset);

      call_order_update_operation op;
      op.funding_account = seller.id;
      op.delta_debt   = mia.amount_from_string(amount_to_borrow);
      op.delta_collateral = collateral.amount_from_string(amount_of_collateral);

      signed_transaction trx;
      trx.operations = {op};
      set_operation_fees( trx, _remote_db->get_global_properties().parameters.current_fees);
      trx.validate();
      idump((broadcast));

      return sign_transaction(trx, broadcast);
   }

   signed_transaction cancel_order(object_id_type order_id, bool broadcast = false)
   { try {
         FC_ASSERT(!is_locked());
         FC_ASSERT(order_id.space() == protocol_ids, "Invalid order ID ${id}", ("id", order_id));
         signed_transaction trx;

         limit_order_cancel_operation op;
         op.fee_paying_account = get_object<limit_order_object>(order_id).seller;
         op.order = order_id;
         trx.operations = {op};
         set_operation_fees( trx, _remote_db->get_global_properties().parameters.current_fees);

         trx.validate();
         return sign_transaction(trx, broadcast);
   } FC_CAPTURE_AND_RETHROW((order_id)) }

   signed_transaction transfer(string from, string to, string amount,
                               string asset_symbol, string memo, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );
      fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
      FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

      account_object from_account = get_account(from);
      account_object to_account = get_account(to);
      account_id_type from_id = from_account.id;
      account_id_type to_id = get_account_id(to);

      transfer_operation xfer_op;

      xfer_op.from = from_id;
      xfer_op.to = to_id;
      xfer_op.amount = asset_obj->amount_from_string(amount);

      if( memo.size() )
         {
            xfer_op.memo = memo_data();
            xfer_op.memo->from = from_account.options.memo_key;
            xfer_op.memo->to = to_account.options.memo_key;
            xfer_op.memo->set_message(get_private_key(from_account.options.memo_key),
                                      to_account.options.memo_key, memo);
         }

      signed_transaction tx;
      tx.operations.push_back(xfer_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (from)(to)(amount)(asset_symbol)(memo)(broadcast) ) }

   signed_transaction transfer_vault_to_wallet(string vault, string wallet, string amount,
                               string asset_symbol, share_type reserved, bool broadcast = false)
   { try {
      FC_ASSERT( !self.is_locked() );
      fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
      FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

      account_object vault_account = get_account(vault);
      account_object wallet_account = get_account(wallet);
      account_id_type vault_id = vault_account.id;
      account_id_type wallet_id = wallet_account.id;

      transfer_vault_to_wallet_operation xfer_op;

      xfer_op.from_vault = vault_id;
      xfer_op.to_wallet = wallet_id;
      xfer_op.asset_to_transfer = asset_obj->amount_from_string(amount);
      xfer_op.reserved_to_transfer = reserved;

      signed_transaction tx;
      tx.operations.push_back(xfer_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (vault)(wallet)(amount)(asset_symbol)(reserved)(broadcast) ) }

   signed_transaction issue_asset(string to_account, string amount, string symbol,
                                  string memo, bool broadcast = false)
   {
      auto asset_obj = get_asset(symbol);

      account_object to = get_account(to_account);
      account_object issuer = get_account(asset_obj.issuer);

      asset_issue_operation issue_op;
      issue_op.issuer           = asset_obj.issuer;
      issue_op.asset_to_issue   = asset_obj.amount_from_string(amount);
      issue_op.issue_to_account = to.id;

      if( memo.size() )
      {
         issue_op.memo = memo_data();
         issue_op.memo->from = issuer.options.memo_key;
         issue_op.memo->to = to.options.memo_key;
         issue_op.memo->set_message(get_private_key(issuer.options.memo_key),
                                    to.options.memo_key, memo);
      }

      signed_transaction tx;
      tx.operations.push_back(issue_op);
      set_operation_fees(tx,_remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction(tx, broadcast);
   }

   signed_transaction issue_webasset(string to_account, string amount, string reserved, string unique_id, bool broadcast)
   {
       const auto asset_obj = get_web_asset();

       account_object to = get_account(to_account);

       asset_create_issue_request_operation issue_op;
       issue_op.issuer = asset_obj.issuer;
       issue_op.receiver = to.id;
       issue_op.amount =
           asset_obj.amount_from_string(amount).amount;
       issue_op.asset_id = asset_obj.id;
       issue_op.reserved_amount =
           asset_obj.amount_from_string(reserved).amount;
       issue_op.unique_id = unique_id;

       signed_transaction tx;
       tx.operations.push_back(issue_op);
       set_operation_fees(tx,_remote_db->get_global_properties().parameters.current_fees);
       tx.validate();

       return sign_transaction(tx, broadcast);
   }

   std::map<string,std::function<string(fc::variant,const fc::variants&)>> get_result_formatters() const
   {
      std::map<string,std::function<string(fc::variant,const fc::variants&)> > m;
      m["help"] = [](variant result, const fc::variants& a)
      {
         return result.get_string();
      };

      m["gethelp"] = [](variant result, const fc::variants& a)
      {
         return result.get_string();
      };

      m["get_account_history"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;

         for( operation_detail& d : r )
         {
            operation_history_object& i = d.op;
            auto b = _remote_db->get_block_header(i.block_num);
            FC_ASSERT(b);
            ss << b->timestamp.to_iso_string() << " ";
            auto opv = operation_printer(ss, *this, i.result);
            i.op.visit(opv);
            ss << " \n";
         }

         return ss.str();
      };

      m["get_account_history_by_operation"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;

         for( operation_detail& d : r )
         {
            operation_history_object& i = d.op;
            auto b = _remote_db->get_block_header(i.block_num);
            FC_ASSERT(b);
            ss << b->timestamp.to_iso_string() << " ";
            ss << d.description << " \n";
         }

         return ss.str();
      };

      m["list_account_balances"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<vector<asset_reserved>>( GRAPHENE_MAX_NESTED_OBJECTS );
         vector<asset_object> asset_recs;
         std::transform(r.begin(), r.end(), std::back_inserter(asset_recs), [this](const asset_reserved& a) {
            return get_asset(a.asset_id);
         });

         std::stringstream ss;
         for( unsigned i = 0; i < asset_recs.size(); ++i )
            ss << asset_recs[i].amount_to_pretty_string(r[i]) << "\n";

         return ss.str();
      };

      m["get_blind_balances"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<vector<asset>>( GRAPHENE_MAX_NESTED_OBJECTS );
         vector<asset_object> asset_recs;
         std::transform(r.begin(), r.end(), std::back_inserter(asset_recs), [this](const asset& a) {
            return get_asset(a.asset_id);
         });

         std::stringstream ss;
         for( unsigned i = 0; i < asset_recs.size(); ++i )
            ss << asset_recs[i].amount_to_pretty_string(r[i]) << "\n";

         return ss.str();
      };
      m["transfer_to_blind"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         auto opv = operation_printer( ss, *this, operation_result());
         r.trx.operations[0].visit( opv  );
         ss << "\n";
         for( const auto& out : r.outputs )
         {
            asset_object a = get_asset( out.decrypted_memo.amount.asset_id );
            ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label << "\n\t  receipt: " << out.confirmation_receipt <<"\n\n";
         }
         return ss.str();
      };
      m["blind_transfer"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         auto opv = operation_printer( ss, *this, operation_result() );
         r.trx.operations[0].visit( opv );
         ss << "\n";
         for( const auto& out : r.outputs )
         {
            asset_object a = get_asset( out.decrypted_memo.amount.asset_id );
            ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label << "\n\t  receipt: " << out.confirmation_receipt <<"\n\n";
         }
         return ss.str();
      };
      m["receive_blind_transfer"] = [this](variant result, const fc::variants& a)
      {
         auto r = result.as<blind_receipt>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         asset_object as = get_asset( r.amount.asset_id );
         ss << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label  << "  " << r.memo <<"\n";
         return ss.str();
      };
      m["blind_history"] = [this](variant result, const fc::variants& a)
      {
         auto records = result.as<vector<blind_receipt>>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         ss << "WHEN         "
            << "  " << "AMOUNT"  << "  " << "FROM" << "  =>  " << "TO" << "  " << "MEMO" <<"\n";
         ss << "====================================================================================\n";
         for( auto& r : records )
         {
            asset_object as = get_asset( r.amount.asset_id );
            ss << fc::get_approximate_relative_time_string( r.date )
               << "  " << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label  << "  " << r.memo <<"\n";
         }
         return ss.str();
      };
      m["get_order_book"] = [this](variant result, const fc::variants& a)
      {
         auto orders = result.as<order_book>( GRAPHENE_MAX_NESTED_OBJECTS );
         auto bids = orders.bids;
         auto asks = orders.asks;
         std::stringstream ss;
         std::stringstream sum_stream;
         sum_stream << "Sum(" << orders.base << ')';
         double bid_sum = 0;
         double ask_sum = 0;
         const int spacing = 20;

         auto prettify_num = [&]( double n )
         {
            //ss << n;
            if (abs( round( n ) - n ) < 0.00000000001 )
            {
               //ss << setiosflags( !ios::fixed ) << (int) n;     // doesn't compile on Linux with gcc
               ss << (int) n;
            }
            else if (n - floor(n) < 0.000001)
            {
               ss << setiosflags( ios::fixed ) << setprecision(10) << n;
            }
            else
            {
               ss << setiosflags( ios::fixed ) << setprecision(6) << n;
            }
         };

         ss << setprecision( 8 ) << setiosflags( ios::fixed ) << setiosflags( ios::left );

         ss << ' ' << setw( (spacing * 4) + 6 ) << "BUY ORDERS" << "SELL ORDERS\n"
            << ' ' << setw( spacing + 1 ) << "Price" << setw( spacing ) << orders.quote << ' ' << setw( spacing )
            << orders.base << ' ' << setw( spacing ) << sum_stream.str()
            << "   " << setw( spacing + 1 ) << "Price" << setw( spacing ) << orders.quote << ' ' << setw( spacing )
            << orders.base << ' ' << setw( spacing ) << sum_stream.str()
            << "\n====================================================================================="
            << "|=====================================================================================\n";

         for (size_t i = 0; i < bids.size() || i < asks.size() ; i++)
         {
            if ( i < bids.size() )
            {
                bid_sum += bids[i].base;
                ss << ' ' << setw( spacing );
                prettify_num( bids[i].price );
                ss << ' ' << setw( spacing );
                prettify_num( bids[i].quote );
                ss << ' ' << setw( spacing );
                prettify_num( bids[i].base );
                ss << ' ' << setw( spacing );
                prettify_num( bid_sum );
                ss << ' ';
            }
            else
            {
                ss << setw( (spacing * 4) + 5 ) << ' ';
            }

            ss << '|';

            if ( i < asks.size() )
            {
               ask_sum += asks[i].base;
               ss << ' ' << setw( spacing );
               prettify_num( asks[i].price );
               ss << ' ' << setw( spacing );
               prettify_num( asks[i].quote );
               ss << ' ' << setw( spacing );
               prettify_num( asks[i].base );
               ss << ' ' << setw( spacing );
               prettify_num( ask_sum );
            }

            ss << '\n';
         }

         ss << endl
            << "Buy Total:  " << bid_sum << ' ' << orders.base << endl
            << "Sell Total: " << ask_sum << ' ' << orders.base << endl;

         return ss.str();
      };

      return m;
   }

   signed_transaction propose_parameter_change(
      const string& proposing_account,
      fc::time_point_sec expiration_time,
      const variant_object& changed_values,
      bool broadcast = false)
   {
      FC_ASSERT( !changed_values.contains("current_fees") );

      const chain_parameters& current_params = get_global_properties().parameters;
      chain_parameters new_params = current_params;
      fc::reflector<chain_parameters>::visit(
         fc::from_variant_visitor<chain_parameters>( changed_values, new_params, GRAPHENE_MAX_NESTED_OBJECTS )
         );

      committee_member_update_global_parameters_operation update_op;
      update_op.new_parameters = new_params;

      proposal_create_operation prop_op;

      prop_op.expiration_time = expiration_time;
      prop_op.review_period_seconds = current_params.committee_proposal_review_period;
      prop_op.fee_paying_account = get_account(proposing_account).id;

      prop_op.proposed_ops.emplace_back( update_op );
      current_params.current_fees->set_fee( prop_op.proposed_ops.back().op );

      signed_transaction tx;
      tx.operations.push_back(prop_op);
      set_operation_fees(tx, current_params.current_fees);
      tx.validate();

      return sign_transaction(tx, broadcast);
   }

   signed_transaction propose_fee_change(
      const string& proposing_account,
      fc::time_point_sec expiration_time,
      const variant_object& changed_fees,
      bool broadcast = false)
   {
      const chain_parameters& current_params = get_global_properties().parameters;
      const fee_schedule_type& current_fees = *(current_params.current_fees);

      flat_map< int, fee_parameters > fee_map;
      fee_map.reserve( current_fees.parameters.size() );
      for( const fee_parameters& op_fee : current_fees.parameters )
         fee_map[ op_fee.which() ] = op_fee;
      uint32_t scale = current_fees.scale;

      for( const auto& item : changed_fees )
      {
         const string& key = item.key();
         if( key == "scale" )
         {
            int64_t _scale = item.value().as_int64();
            FC_ASSERT( _scale >= 0 );
            FC_ASSERT( _scale <= std::numeric_limits<uint32_t>::max() );
            scale = uint32_t( _scale );
            continue;
         }
         // is key a number?
         auto is_numeric = [&]() -> bool
         {
            size_t n = key.size();
            for( size_t i=0; i<n; i++ )
            {
               if( !isdigit( key[i] ) )
                  return false;
            }
            return true;
         };

         int which;
         if( is_numeric() )
            which = std::stoi( key );
         else
         {
            const auto& n2w = _operation_which_map.name_to_which;
            auto it = n2w.find( key );
            FC_ASSERT( it != n2w.end(), "unknown operation" );
            which = it->second;
         }

         fee_parameters fp = from_which_variant< fee_parameters >( which, item.value(), GRAPHENE_MAX_NESTED_OBJECTS );
         fee_map[ which ] = fp;
      }

      fee_schedule_type new_fees;

      for( const std::pair< int, fee_parameters >& item : fee_map )
         new_fees.parameters.insert( item.second );
      new_fees.scale = scale;

      chain_parameters new_params = current_params;
      new_params.current_fees = new_fees;

      committee_member_update_global_parameters_operation update_op;
      update_op.new_parameters = new_params;

      proposal_create_operation prop_op;

      prop_op.expiration_time = expiration_time;
      prop_op.review_period_seconds = current_params.committee_proposal_review_period;
      prop_op.fee_paying_account = get_account(proposing_account).id;

      prop_op.proposed_ops.emplace_back( update_op );
      current_params.current_fees->set_fee( prop_op.proposed_ops.back().op );

      signed_transaction tx;
      tx.operations.push_back(prop_op);
      set_operation_fees(tx, current_params.current_fees);
      tx.validate();

      return sign_transaction(tx, broadcast);
   }

   signed_transaction approve_proposal(
      const string& fee_paying_account,
      const string& proposal_id,
      const approval_delta& delta,
      bool broadcast = false)
   {
      proposal_update_operation update_op;

      update_op.fee_paying_account = get_account(fee_paying_account).id;
      update_op.proposal = fc::variant(proposal_id, 1).as<proposal_id_type>( 1 );
      // make sure the proposal exists
      get_object( update_op.proposal );

      for( const std::string& name : delta.active_approvals_to_add )
         update_op.active_approvals_to_add.insert( get_account( name ).id );
      for( const std::string& name : delta.active_approvals_to_remove )
         update_op.active_approvals_to_remove.insert( get_account( name ).id );
      for( const std::string& name : delta.owner_approvals_to_add )
         update_op.owner_approvals_to_add.insert( get_account( name ).id );
      for( const std::string& name : delta.owner_approvals_to_remove )
         update_op.owner_approvals_to_remove.insert( get_account( name ).id );
      for( const std::string& k : delta.key_approvals_to_add )
         update_op.key_approvals_to_add.insert( public_key_type( k ) );
      for( const std::string& k : delta.key_approvals_to_remove )
         update_op.key_approvals_to_remove.insert( public_key_type( k ) );

      signed_transaction tx;
      tx.operations.push_back(update_op);
      set_operation_fees(tx, get_global_properties().parameters.current_fees);
      tx.validate();
      return sign_transaction(tx, broadcast);
   }

   /////////////////////////////
   //                         //
   // LICENSES:               //
   //                         //
   /////////////////////////////

   // INTERNAL METHODS:

   optional<license_type_object> find_license_type(license_type_id_type id)const
   {
      return _remote_db->get_license_type(id);
   }

   optional<license_type_object> find_license_type(string str_or_id)const
   {
      FC_ASSERT( str_or_id.size() > 0 );

      if( auto id = maybe_id<license_type_id_type>(str_or_id) )
      {
         // It's an ID
         return find_license_type(*id);
      } else {
         // It's a name
         auto rec = _remote_db->lookup_license_type_names({str_or_id}).front();
         if( rec )
         {
            if( rec->name != str_or_id )
               return optional<license_type_object>();

            _license_type_cache[rec->id] = *rec;
         }
         return rec;
      }
   }

   license_type_object get_license_type(license_type_id_type id)const
   {
      auto opt = find_license_type(id);
      FC_ASSERT(opt);
      return *opt;
   }

   license_type_object get_license_type(string str_or_id)const
   {
      auto opt = find_license_type(str_or_id);
      FC_ASSERT(opt);
      return *opt;
   }

   /**
    * Issue a license to an account. This will create the license request that can be denied by the license
    * authentication authority.  Only the license issuing authority can do this.
    * @param  issuer    This MUST be the license issuing authority.
    * @param  account   Account to benefit the license.
    * @param  license   License type id to be issued.
    * @param  broadcast true if you wish to broadcast the transaction.
    * @return           Signed version of the transaction.
    */
   signed_transaction issue_license(
      const string& issuer,
      const string& account,
      const string& license,
      share_type bonus_percentage,
      frequency_type frequency,
      bool broadcast)
   {
      auto issuer_account = this->get_account( issuer );
      auto beneficiary = get_account( account );
      auto new_license = get_license_type( license );

      issue_license_operation op;

      op.issuer = issuer_account.id;
      op.account = beneficiary.id;
      op.license = new_license.id;
      op.bonus_percentage = bonus_percentage;
      op.frequency_lock = frequency;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction submit_cycles_to_queue_by_license(
      const string& account,
      share_type amount,
      const string& license,
      frequency_type frequency,
      const string& comment,
      bool broadcast)
   {
      auto account_id = get_account( account ).id;
      auto license_type = get_license_type( license );

      submit_cycles_to_queue_by_license_operation op;

      op.account = account_id;
      op.amount = amount;
      op.license_type = license_type.id;
      op.frequency_lock = frequency;
      op.comment = comment;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction update_queue_parameters(
       optional<bool> enable_dascoin_queue,
       optional<uint32_t> reward_interval_time_seconds,
       optional<share_type> dascoin_reward_amount, bool broadcast) {
     
     auto issuer_id = _remote_db->get_global_properties().authorities.license_issuer;

     signed_transaction tx;
     tx.operations.push_back(update_queue_parameters_operation(
         issuer_id, enable_dascoin_queue, reward_interval_time_seconds,
         dascoin_reward_amount));
     set_operation_fees(
         tx, _remote_db->get_global_properties().parameters.current_fees);
     tx.validate();

     return sign_transaction(tx, broadcast);
   }

   signed_transaction wire_out(const string& account_name, share_type amount, bool broadcast)
   {
      auto account = get_account(account_name);

      wire_out_operation op;
      op.account = account.id;
      op.asset_to_wire = asset(amount, asset_id_type(DASCOIN_WEB_ASSET_INDEX));

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees(tx, _remote_db->get_global_properties().parameters.current_fees);

      return sign_transaction(tx, broadcast);
   }

   signed_transaction wire_out_with_fee(const string& account_name, share_type amount, const string& currency_of_choice,
                                        const string& to_address, const string& memo, bool broadcast)
   {
     auto account = get_account(account_name);

     wire_out_with_fee_operation op;
     op.account = account.id;
     op.asset_to_wire = asset(amount, asset_id_type(DASCOIN_WEB_ASSET_INDEX));
     op.currency_of_choice = currency_of_choice;
     op.to_address = to_address;
     op.memo = memo;

     signed_transaction tx;
     tx.operations.push_back(op);
     set_operation_fees(tx, _remote_db->get_global_properties().parameters.current_fees);

     return sign_transaction(tx, broadcast);
   }


   signed_transaction set_roll_back_enabled(const string& account_name, bool roll_back_enabled, bool broadcast)
   {
     set_roll_back_enabled_operation op;
     op.account = get_account(account_name).id;
     op.roll_back_enabled = roll_back_enabled;
     signed_transaction tx;
     tx.operations.push_back(op);
     set_operation_fees(tx, _remote_db->get_global_properties().parameters.current_fees);
     return sign_transaction(tx, broadcast);
   }

   signed_transaction roll_back_public_keys(const string& authority, const string& account_name, bool broadcast)
   {
     roll_back_public_keys_operation op;
     op.authority = get_account(authority).id;
     op.account = get_account(account_name).id;
     signed_transaction tx;
     tx.operations.push_back(op);
     set_operation_fees(tx, _remote_db->get_global_properties().parameters.current_fees);
     return sign_transaction(tx, broadcast);
   }

   signed_transaction set_chain_authority(const string& issuer, const string& account, const string& kind, bool broadcast)
   {
    try {
      FC_ASSERT( !self.is_locked() );

      set_chain_authority_operation op;

      op.issuer = get_account(issuer).id;
      op.account = get_account(account).id;
      op.kind = kind;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees(tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction(tx, broadcast);
    } FC_CAPTURE_AND_RETHROW( (issuer)(account)(kind)(broadcast) )
   }

   void dbg_make_uia(string creator, string symbol)
   {
      asset_options opts;
      opts.flags &= ~(white_list | disable_force_settle | global_settle);
      opts.issuer_permissions = opts.flags;
      opts.core_exchange_rate = price(asset(1), asset(1,asset_id_type(1)));
      create_asset(get_account(creator).name, symbol, 2, opts, {}, true);
   }

   void dbg_make_mia(string creator, string symbol)
   {
      asset_options opts;
      opts.flags &= ~white_list;
      opts.issuer_permissions = opts.flags;
      opts.core_exchange_rate = price(asset(1), asset(1,asset_id_type(1)));
      bitasset_options bopts;
      create_asset(get_account(creator).name, symbol, 2, opts, bopts, true);
   }

   void dbg_push_blocks( const std::string& src_filename, uint32_t count )
   {
      use_debug_api();
      (*_remote_debug)->debug_push_blocks( src_filename, count );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void dbg_generate_blocks( const std::string& debug_wif_key, uint32_t count )
   {
      use_debug_api();
      (*_remote_debug)->debug_generate_blocks( debug_wif_key, count );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void dbg_stream_json_objects( const std::string& filename )
   {
      use_debug_api();
      (*_remote_debug)->debug_stream_json_objects( filename );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void dbg_update_object( const fc::variant_object& update )
   {
      use_debug_api();
      (*_remote_debug)->debug_update_object( update );
      (*_remote_debug)->debug_stream_json_objects_flush();
   }

   void use_network_node_api()
   {
      if( _remote_net_node )
         return;
      try
      {
         _remote_net_node = _remote_api->network_node();
      }
      catch( const fc::exception& e )
      {
         std::cerr << "\nCouldn't get network node API.  You probably are not configured\n"
         "to access the network API on the witness_node you are\n"
         "connecting to.  Please follow the instructions in README.md to set up an apiaccess file.\n"
         "\n";
         throw(e);
      }
   }

   void use_debug_api()
   {
      if( _remote_debug )
         return;
      try
      {
        _remote_debug = _remote_api->debug();
      }
      catch( const fc::exception& e )
      {
         std::cerr << "\nCouldn't get debug node API.  You probably are not configured\n"
         "to access the debug API on the node you are connecting to.\n"
         "\n"
         "To fix this problem:\n"
         "- Please ensure you are running debug_node, not witness_node.\n"
         "- Please follow the instructions in README.md to set up an apiaccess file.\n"
         "\n";
      }
   }

   void network_add_nodes( const vector<string>& nodes )
   {
      use_network_node_api();
      for( const string& node_address : nodes )
      {
         (*_remote_net_node)->add_node( fc::ip::endpoint::from_string( node_address ) );
      }
   }

   vector< variant > network_get_connected_peers()
   {
      use_network_node_api();
      const auto peers = (*_remote_net_node)->get_connected_peers();
      vector< variant > result;
      result.reserve( peers.size() );
      for( const auto& peer : peers )
      {
         variant v;
         fc::to_variant( peer, v, GRAPHENE_MAX_NESTED_OBJECTS );
         result.push_back( v );
      }
      return result;
   }

   void flood_network(string prefix, uint32_t number_of_transactions)
   {
      try
      {
         const account_object& master = *_wallet.my_accounts.get<by_name>().lower_bound("import");
         int number_of_accounts = number_of_transactions / 3;
         number_of_transactions -= number_of_accounts;
         //auto key = derive_private_key("floodshill", 0);
         try {
            dbg_make_uia(master.name, "SHILL");
         } catch(...) {/* Ignore; the asset probably already exists.*/}

         fc::time_point start = fc::time_point::now();
         for( int i = 0; i < number_of_accounts; ++i )
         {
            std::ostringstream brain_key;
            brain_key << "brain key for account " << prefix << i;
            signed_transaction trx = create_account_with_brain_key(brain_key.str(), prefix + fc::to_string(i), /* broadcast = */ true, /* save wallet = */ false);
         }
         fc::time_point end = fc::time_point::now();
         ilog("Created ${n} accounts in ${time} milliseconds",
              ("n", number_of_accounts)("time", (end - start).count() / 1000));

         start = fc::time_point::now();
         for( int i = 0; i < number_of_accounts; ++i )
         {
            signed_transaction trx = transfer(master.name, prefix + fc::to_string(i), "10", "CORE", "", true);
            trx = transfer(master.name, prefix + fc::to_string(i), "1", "CORE", "", true);
         }
         end = fc::time_point::now();
         ilog("Transferred to ${n} accounts in ${time} milliseconds",
              ("n", number_of_accounts*2)("time", (end - start).count() / 1000));

         start = fc::time_point::now();
         for( int i = 0; i < number_of_accounts; ++i )
         {
            signed_transaction trx = issue_asset(prefix + fc::to_string(i), "1000", "SHILL", "", true);
         }
         end = fc::time_point::now();
         ilog("Issued to ${n} accounts in ${time} milliseconds",
              ("n", number_of_accounts)("time", (end - start).count() / 1000));
      }
      catch (...)
      {
         throw;
      }

   }

   operation get_prototype_operation( string operation_name )
   {
      auto it = _prototype_ops.find( operation_name );
      if( it == _prototype_ops.end() )
         FC_THROW("Unsupported operation: \"${operation_name}\"", ("operation_name", operation_name));
      return it->second;
   }

   string                  _wallet_filename;
   wallet_data             _wallet;

   map<public_key_type,string> _keys;
   fc::sha512                  _checksum;

   chain_id_type           _chain_id;
   fc::api<login_api>      _remote_api;
   fc::api<database_api>   _remote_db;
   fc::api<network_broadcast_api>   _remote_net_broadcast;
   fc::api<history_api>    _remote_hist;
   optional< fc::api<network_node_api> > _remote_net_node;
   optional< fc::api<graphene::debug_witness::debug_api> > _remote_debug;

   flat_map<string, operation> _prototype_ops;

   static_variant_map _operation_which_map = create_static_variant_map< operation >();

#ifdef __unix__
   mode_t                  _old_umask;
#endif
   const string _wallet_filename_extension = ".wallet";

   mutable map<asset_id_type, asset_object> _asset_cache;
   mutable map<license_type_id_type, license_type_object> _license_type_cache;
};


template<typename T>
void account_asset_cache_visitor::operator()(const T& op) {

   asset_cache.insert(op.fee.asset_id);
   account_cache.insert( op.fee_payer());

   account_asset_result_cache_visitor aarcv(this);
   result.visit(aarcv);
}


void account_asset_cache_visitor::operator()(const transfer_from_blind_operation& op)
{
   asset_cache.insert(op.fee.asset_id);
   account_cache.insert( op.to);
}
void account_asset_cache_visitor::operator()(const transfer_to_blind_operation& op)
{
   asset_cache.insert(op.fee.asset_id);
   asset_cache.insert(op.amount.asset_id);
   account_cache.insert( op.from);
}
void account_asset_cache_visitor::operator()(const transfer_operation& op)
{
   asset_cache.insert(op.amount.asset_id);
   account_cache.insert( op.from);
   account_cache.insert( op.to);

   asset_cache.insert(op.fee.asset_id);
}

void account_asset_cache_visitor::operator()(const account_create_operation& op)
{
   asset_cache.insert(op.fee.asset_id);
}

void account_asset_cache_visitor::operator()(const account_update_operation& op)
{
   account_cache.insert( op.account);
   asset_cache.insert(op.fee.asset_id);
}

void account_asset_cache_visitor::operator()(const asset_create_operation& op)
{
   account_cache.insert( op.issuer);
   asset_cache.insert(op.fee.asset_id);
}

void account_asset_result_cache_visitor::operator()(const void_result& x) const
{
   return;
}

void account_asset_result_cache_visitor::operator()(const object_id_type& oid)
{
   return;
}

void account_asset_result_cache_visitor::operator()(const asset& a)
{
   _av->asset_cache.insert(a.asset_id);
}



asset_object operation_printer::get_asset(asset_id_type id) {
   if(use_cache)
   {
      auto it = cached_assets.find(id);
      if(it != cached_assets.end())
      {
         return it->second;
      }

      cached_assets[id] = wallet.get_asset(id);
      return cached_assets[id];
   }

   return wallet.get_asset(id);
}

account_object operation_printer::get_account(account_id_type id) {
   if(use_cache)
   {
      auto it = cached_accounts.find(id);
      if(it != cached_accounts.end())
      {
         return it->second;
      }
      cached_accounts[id] = wallet.get_account(id);
      return cached_accounts[id];
   }

   return wallet.get_account(id);
}

std::string operation_printer::fee(const asset& a) {

   out << "   (Fee: " << get_asset(a.asset_id).amount_to_pretty_string(a) << ")";
   return "";
}

template<typename T>
std::string operation_printer::operator()(const T& op)
{
   //balance_accumulator acc;
   //op.get_balance_delta( acc, result );
   auto a = get_asset( op.fee.asset_id );
   auto payer = get_account( op.fee_payer() );

   string op_name = fc::get_typename<T>::name();
   if( op_name.find_last_of(':') != string::npos )
      op_name.erase(0, op_name.find_last_of(':')+1);
   out << op_name <<" ";
  // out << "balance delta: " << fc::json::to_string(acc.balance) <<"   ";
   out << payer.name << " fee: " << a.amount_to_pretty_string( op.fee );
   operation_result_printer rprinter(this);
   std::string str_result = result.visit(rprinter);
   if( str_result != "" )
   {
      out << "   result: " << str_result;
   }
   return "";
}

std::string operation_printer::operator()(const transfer_from_blind_operation& op)
{
   auto a = get_asset( op.fee.asset_id );
   auto receiver = get_account( op.to );

   out <<  receiver.name
   << " received " << a.amount_to_pretty_string( op.amount ) << " from blinded balance";
   return "";
}
std::string operation_printer::operator()(const transfer_to_blind_operation& op)
{
   auto fa = get_asset( op.fee.asset_id );
   auto a = get_asset( op.amount.asset_id );
   auto sender = get_account( op.from );

   out <<  sender.name
   << " sent " << a.amount_to_pretty_string( op.amount ) << " to " << op.outputs.size() << " blinded balance" << (op.outputs.size()>1?"s":"")
   << " fee: " << fa.amount_to_pretty_string( op.fee );
   return "";
}
string operation_printer::operator()(const transfer_operation& op)
{
   out << "Transfer " << get_asset(op.amount.asset_id).amount_to_pretty_string(op.amount)
       << " from " << get_account(op.from).name << " to " << get_account(op.to).name;
   std::string memo;
   if( op.memo )
   {
      if( wallet.is_locked() )
      {
         out << " -- Unlock wallet to see memo.";
      } else {
         try {
            FC_ASSERT(wallet._keys.count(op.memo->to) || wallet._keys.count(op.memo->from), "Memo is encrypted to a key ${to} or ${from} not in this wallet.", ("to", op.memo->to)("from",op.memo->from));
            if( wallet._keys.count(op.memo->to) ) {
               auto my_key = wif_to_key(wallet._keys.at(op.memo->to));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               memo = op.memo->get_message(*my_key, op.memo->from);
               out << " -- Memo: " << memo;
            } else {
               auto my_key = wif_to_key(wallet._keys.at(op.memo->from));
               FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
               memo = op.memo->get_message(*my_key, op.memo->to);
               out << " -- Memo: " << memo;
            }
         } catch (const fc::exception& e) {
            out << " -- could not decrypt memo";
            elog("Error when decrypting memo: ${e}", ("e", e.to_detail_string()));
         }
      }
   }
   fee(op.fee);
   return memo;
}

std::string operation_printer::operator()(const account_create_operation& op)
{
   out << "Create Account '" << op.name << "'";
   return fee(op.fee);
}

std::string operation_printer::operator()(const account_update_operation& op)
{
   out << "Update Account '" << get_account(op.account).name << "'";
   return fee(op.fee);
}

std::string operation_printer::operator()(const asset_create_operation& op)
{
   out << "Create ";
   if( op.bitasset_opts.valid() )
      out << "BitAsset ";
   else
      out << "User-Issue Asset ";
   out << "'" << op.symbol << "' with issuer " << get_account(op.issuer).name;
   return fee(op.fee);
}

std::string operation_printer::operator()(const daspay_debit_account_operation& op)
{
   auto a = get_asset( op.debit_amount.asset_id );
   auto payer = get_account( op.fee_payer() );

   out << "Daspay debit " << payer.name << " (" << std::string(payer.id) << ") for " << a.amount_to_pretty_string(op.debit_amount);
   operation_result_printer rprinter(this);
   std::string str_result = result.visit(rprinter);
   if( str_result != "" )
   {
      out << " charged: " << str_result;
   }
   return "";
}

std::string operation_result_printer::operator()(const void_result& x) const
{
   return "";
}

std::string operation_result_printer::operator()(const object_id_type& oid)
{
   return std::string(oid);
}

std::string operation_result_printer::operator()(const asset& a)
{
   return _operation_printer->get_asset(a.asset_id).amount_to_pretty_string(a);
}

}}}

namespace graphene { namespace wallet {
    vector<brain_key_info> utility::derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys)
    {
      // Safety-check
      FC_ASSERT( number_of_desired_keys >= 1 );

      // Create as many derived owner keys as requested
      vector<brain_key_info> results;
      brain_key = graphene::wallet::detail::normalize_brain_key(brain_key);
      for (int i = 0; i < number_of_desired_keys; ++i) {
        fc::ecc::private_key priv_key = graphene::wallet::detail::derive_private_key( brain_key, i );

        brain_key_info result;
        result.brain_priv_key = brain_key;
        result.wif_priv_key = key_to_wif( priv_key );
        result.pub_key = priv_key.get_public_key();

        results.push_back(result);
      }

      return results;
    }

    brain_key_info utility::suggest_brain_key()
    {
      brain_key_info result;
      // create a private key for secure entropy
      fc::sha256 sha_entropy1 = fc::ecc::private_key::generate().get_secret();
      fc::sha256 sha_entropy2 = fc::ecc::private_key::generate().get_secret();
      fc::bigint entropy1(sha_entropy1.data(), sha_entropy1.data_size());
      fc::bigint entropy2(sha_entropy2.data(), sha_entropy2.data_size());
      fc::bigint entropy(entropy1);
      entropy <<= 8 * sha_entropy1.data_size();
      entropy += entropy2;
      string brain_key = "";

      for (int i = 0; i < BRAIN_KEY_WORD_COUNT; i++)
      {
        fc::bigint choice = entropy % graphene::words::word_list_size;
        entropy /= graphene::words::word_list_size;
        if (i > 0)
          brain_key += " ";
        brain_key += graphene::words::word_list[choice.to_int64()];
      }

      brain_key = detail::normalize_brain_key(brain_key);
      fc::ecc::private_key priv_key = detail::derive_private_key(brain_key, 0);
      result.brain_priv_key = brain_key;
      result.wif_priv_key = key_to_wif(priv_key);
      result.pub_key = priv_key.get_public_key();
      return result;
    }
  }}

namespace graphene { namespace wallet {

wallet_api::wallet_api(const wallet_data& initial_data, fc::api<login_api> rapi)
   : my(new detail::wallet_api_impl(*this, initial_data, rapi))
{
}

wallet_api::~wallet_api()
{
}

brain_key_info wallet_api::suggest_brain_key()const
{
  return graphene::wallet::utility::suggest_brain_key();
}

vector<brain_key_info> wallet_api::derive_owner_keys_from_brain_key(string brain_key, int number_of_desired_keys) const
{
  return graphene::wallet::utility::derive_owner_keys_from_brain_key(brain_key, number_of_desired_keys);
}

bool wallet_api::copy_wallet_file(string destination_filename)
{
   return my->copy_wallet_file(destination_filename);
}

optional<signed_block_with_info> wallet_api::get_block(uint32_t num)
{
   return my->_remote_db->get_block(num);
}

uint64_t wallet_api::get_account_count() const
{
   return my->_remote_db->get_account_count();
}

vector<account_object> wallet_api::list_my_accounts()
{
   return vector<account_object>(my->_wallet.my_accounts.begin(), my->_wallet.my_accounts.end());
}

map<string,account_id_type> wallet_api::list_accounts(const string& lowerbound, uint32_t limit)
{
   return my->_remote_db->lookup_accounts(lowerbound, limit);
}

vector<asset_reserved> wallet_api::list_account_balances(const string& id)
{
   if( auto real_id = detail::maybe_id<account_id_type>(id) )
      return my->_remote_db->get_account_balances(*real_id, flat_set<asset_id_type>());
   return my->_remote_db->get_account_balances(get_account(id).id, flat_set<asset_id_type>());
}

vector<asset_object> wallet_api::list_assets(const string& lowerbound, uint32_t limit)const
{
   return my->_remote_db->list_assets( lowerbound, limit );
}

vector<operation_detail> wallet_api::get_account_history(string name, int limit)const
{
   vector<operation_detail> result;
   auto account_id = get_account(name).get_id();

   while( limit > 0 )
   {
      operation_history_id_type start;
      if( result.size() )
      {
         start = result.back().op.id;
         start = start + 1;
      }

      vector<operation_history_object> current = my->_remote_hist->get_account_history(account_id, operation_history_id_type(), std::min(100,limit), start);
      for( auto& o : current ) {
         std::stringstream ss;
         auto opv = detail::operation_printer(ss, *my, o.result);
         auto memo = o.op.visit(opv);
         result.push_back( operation_detail{ memo, ss.str(), o } );
      }
      if( current.size() < static_cast<vector<operation_history_object>::size_type>(std::min(100,limit)) )
         break;
      limit -= current.size();
   }

   return result;
}

// fixme: refactor this
vector<operation_detail> wallet_api::get_account_history_by_operation(string name, flat_set<uint32_t> operations, int limit)const
{
   vector<operation_detail> result;
   auto account_id = get_account(name).get_id();

   while( limit > 0 )
   {
      operation_history_id_type start;
      if( result.size() )
      {
         start = result.back().op.id;
         start = start + 1;
      }

      vector<operation_history_object> current = my->_remote_hist->get_account_history_by_operation(account_id, operations, operation_history_id_type(), std::min(100,limit), start);

      std::set<account_id_type> account_ids;
      std::set<asset_id_type> asset_ids;
      for( auto& o : current ) {
         auto aacv = detail::account_asset_cache_visitor(asset_ids, account_ids, o.result);
         o.op.visit(aacv);
      }

      std::vector<object_id_type> acc_ids(account_ids.begin(), account_ids.end());
      std::vector<object_id_type> ast_ids(asset_ids.begin(),asset_ids.end());

      std::map<account_id_type, account_object> cached_accounts;
      std::map<asset_id_type, asset_object> cached_assets;

      // get all account objects
      auto account_object_variant_vec = my->_remote_db->get_objects(acc_ids);
      for( size_t i = 0; i < account_object_variant_vec.size(); i++ ) {
         cached_accounts[acc_ids[i]] = account_object_variant_vec[i].as<account_object>( GRAPHENE_MAX_NESTED_OBJECTS );
      }

      // get all asset objects
      auto asset_object_variant_vec = my->_remote_db->get_objects(ast_ids);
      for( size_t i = 0; i < asset_object_variant_vec.size(); i++ ) {
         cached_assets[ast_ids[i]] = asset_object_variant_vec[i].as<asset_object>( GRAPHENE_MAX_NESTED_OBJECTS );
      }

      for( auto& o : current ) {
         std::stringstream ss;
         auto opv = detail::operation_printer(ss, *my, o.result, true, cached_accounts, cached_assets);
         auto memo = o.op.visit(opv);
         result.push_back( operation_detail{ memo, ss.str(), o } );
      }
      if( current.size() < static_cast<vector<operation_history_object>::size_type>(std::min(100,limit)) )
         break;
      limit -= current.size();
   }

   return result;
}

vector<operation_detail> wallet_api::get_account_history_by_operation2(string name, flat_set<uint32_t> operations
      , string start_str, string end_str, int limit)const
{
   optional<operation_history_id_type> start_o = detail::maybe_id<operation_history_id_type>(start_str);
   optional<operation_history_id_type> end_o = detail::maybe_id<operation_history_id_type>(end_str);

   FC_ASSERT(start_o.valid() && end_o.valid(), "Incorrect format of start or end argument.");

   operation_history_id_type start = *start_o;
   operation_history_id_type end = *end_o;

   vector<operation_detail> result;
   auto account_id = get_account(name).get_id();

   while( limit > 0 )
   {
      if( result.size() )
      {
         start = result.back().op.id;
         if(start == end)
            break;
         start = start + 1;
      }

      vector<operation_history_object> current = my->_remote_hist->get_account_history_by_operation(account_id, operations, end, std::min(100,limit), start);
      for( auto& o : current ) {
         std::stringstream ss;
         auto opv = detail::operation_printer(ss, *my, o.result);
         auto memo = o.op.visit(opv);
         result.push_back( operation_detail{ memo, ss.str(), o } );
      }
      if( current.size() < static_cast<vector<operation_history_object>::size_type>(std::min(100,limit)))
         break;
      limit -= current.size();
   }

   return result;
}

vector<bucket_object> wallet_api::get_market_history( string symbol1, string symbol2, uint32_t bucket )const
{
   return my->_remote_hist->get_market_history( get_asset_id(symbol1), get_asset_id(symbol2), bucket, fc::time_point_sec(), fc::time_point::now() );
}

vector<limit_order_object> wallet_api::get_limit_orders(string a, string b, uint32_t limit)const
{
   return my->_remote_db->get_limit_orders(get_asset(a).id, get_asset(b).id, limit);
}

vector<call_order_object> wallet_api::get_call_orders(string a, uint32_t limit)const
{
   return my->_remote_db->get_call_orders(get_asset(a).id, limit);
}

vector<force_settlement_object> wallet_api::get_settle_orders(string a, uint32_t limit)const
{
   return my->_remote_db->get_settle_orders(get_asset(a).id, limit);
}

string wallet_api::serialize_transaction( signed_transaction tx )const
{
   return fc::to_hex(fc::raw::pack(tx));
}

variant wallet_api::get_object( object_id_type id ) const
{
   return my->_remote_db->get_objects({id});
}

string wallet_api::get_wallet_filename() const
{
   return my->get_wallet_filename();
}

transaction_handle_type wallet_api::begin_builder_transaction()
{
   return my->begin_builder_transaction();
}

void wallet_api::add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op)
{
   my->add_operation_to_builder_transaction(transaction_handle, op);
}

void wallet_api::replace_operation_in_builder_transaction(transaction_handle_type handle, unsigned operation_index, const operation& new_op)
{
   my->replace_operation_in_builder_transaction(handle, operation_index, new_op);
}

asset wallet_api::set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset)
{
   return my->set_fees_on_builder_transaction(handle, fee_asset);
}

transaction wallet_api::preview_builder_transaction(transaction_handle_type handle)
{
   return my->preview_builder_transaction(handle);
}

signed_transaction wallet_api::sign_builder_transaction(transaction_handle_type transaction_handle,
                                                        optional<vector<string>> wif_keys, bool broadcast)
{
    return my->sign_builder_transaction(transaction_handle, wif_keys, broadcast);
}

signed_transaction wallet_api::propose_builder_transaction(
   transaction_handle_type handle,
   time_point_sec expiration,
   uint32_t review_period_seconds,
   bool broadcast)
{
   return my->propose_builder_transaction(handle, expiration, review_period_seconds, broadcast);
}

signed_transaction wallet_api::propose_builder_transaction2(
   transaction_handle_type handle,
   string account_name_or_id,
   time_point_sec expiration,
   uint32_t review_period_seconds,
   bool broadcast)
{
   return my->propose_builder_transaction2(handle, account_name_or_id, expiration, review_period_seconds, broadcast);
}

void wallet_api::remove_builder_transaction(transaction_handle_type handle)
{
   return my->remove_builder_transaction(handle);
}

account_object wallet_api::get_account(string account_name_or_id) const
{
   return my->get_account(account_name_or_id);
}

asset_object wallet_api::get_asset(string asset_name_or_id) const
{
   auto a = my->find_asset(asset_name_or_id);
   FC_ASSERT(a);
   return *a;
}

asset_bitasset_data_object wallet_api::get_bitasset_data(string asset_name_or_id) const
{
   auto asset = get_asset(asset_name_or_id);
   FC_ASSERT(asset.is_market_issued() && asset.bitasset_data_id);
   return my->get_object<asset_bitasset_data_object>(*asset.bitasset_data_id);
}

account_id_type wallet_api::get_account_id(string account_name_or_id) const
{
   return my->get_account_id(account_name_or_id);
}

asset_id_type wallet_api::get_asset_id(string asset_symbol_or_id) const
{
   return my->get_asset_id(asset_symbol_or_id);
}

bool wallet_api::import_key(string account_name_or_id, string wif_key)
{
   FC_ASSERT(!is_locked());
   // backup wallet
   fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
   if (!optional_private_key)
      FC_THROW("Invalid private key");
   string shorthash = detail::address_to_shorthash(optional_private_key->get_public_key());
   copy_wallet_file( "before-import-key-" + shorthash );

   if( my->import_key(account_name_or_id, wif_key) )
   {
      save_wallet_file();
      copy_wallet_file( "after-import-key-" + shorthash );
      return true;
   }
   return false;
}

map<string, bool> wallet_api::import_accounts( string filename, string password )
{
   FC_ASSERT( !is_locked() );
   FC_ASSERT( fc::exists( filename ) );

   const auto imported_keys = fc::json::from_file<exported_keys>( filename );

   const auto password_hash = fc::sha512::hash( password );
   FC_ASSERT( fc::sha512::hash( password_hash ) == imported_keys.password_checksum );

   map<string, bool> result;
   for( const auto& item : imported_keys.account_keys )
   {
       const auto import_this_account = [ & ]() -> bool
       {
           try
           {
               const account_object account = get_account( item.account_name );
               const auto& owner_keys = account.owner.get_keys();
               const auto& active_keys = account.active.get_keys();

               for( const auto& public_key : item.public_keys )
               {
                   if( std::find( owner_keys.begin(), owner_keys.end(), public_key ) != owner_keys.end() )
                       return true;

                   if( std::find( active_keys.begin(), active_keys.end(), public_key ) != active_keys.end() )
                       return true;
               }
           }
           catch( ... )
           {
           }

           return false;
       };

       const auto should_proceed = import_this_account();
       result[ item.account_name ] = should_proceed;

       if( should_proceed )
       {
           uint32_t import_successes = 0;
           uint32_t import_failures = 0;
           // TODO: First check that all private keys match public keys
           for( const auto& encrypted_key : item.encrypted_private_keys )
           {
               try
               {
                  const auto plain_text = fc::aes_decrypt( password_hash, encrypted_key );
                  const auto private_key = fc::raw::unpack<private_key_type>( plain_text );

                  import_key( item.account_name, string( graphene::utilities::key_to_wif( private_key ) ) );
                  ++import_successes;
               }
               catch( const fc::exception& e )
               {
                  elog( "Couldn't import key due to exception ${e}", ("e", e.to_detail_string()) );
                  ++import_failures;
               }
           }
           ilog( "successfully imported ${n} keys for account ${name}", ("n", import_successes)("name", item.account_name) );
           if( import_failures > 0 )
              elog( "failed to import ${n} keys for account ${name}", ("n", import_failures)("name", item.account_name) );
       }
   }

   return result;
}

bool wallet_api::import_account_keys( string filename, string password, string src_account_name, string dest_account_name )
{
   FC_ASSERT( !is_locked() );
   FC_ASSERT( fc::exists( filename ) );

   bool is_my_account = false;
   const auto accounts = list_my_accounts();
   for( const auto& account : accounts )
   {
       if( account.name == dest_account_name )
       {
           is_my_account = true;
           break;
       }
   }
   FC_ASSERT( is_my_account );

   const auto imported_keys = fc::json::from_file<exported_keys>( filename );

   const auto password_hash = fc::sha512::hash( password );
   FC_ASSERT( fc::sha512::hash( password_hash ) == imported_keys.password_checksum );

   bool found_account = false;
   for( const auto& item : imported_keys.account_keys )
   {
       if( item.account_name != src_account_name )
           continue;

       found_account = true;

       for( const auto& encrypted_key : item.encrypted_private_keys )
       {
           const auto plain_text = fc::aes_decrypt( password_hash, encrypted_key );
           const auto private_key = fc::raw::unpack<private_key_type>( plain_text );

           my->import_key( dest_account_name, string( graphene::utilities::key_to_wif( private_key ) ) );
       }

       return true;
   }
   save_wallet_file();

   FC_ASSERT( found_account );

   return false;
}

string wallet_api::normalize_brain_key(string s) const
{
   return detail::normalize_brain_key( s );
}

variant wallet_api::info()
{
   return my->info();
}

variant_object wallet_api::about() const
{
    return my->about();
}

fc::ecc::private_key wallet_api::derive_private_key(const std::string& prefix_string, int sequence_number) const
{
   return detail::derive_private_key( prefix_string, sequence_number );
}

signed_transaction wallet_api::register_account(string name,
                                                public_key_type owner_pubkey,
                                                public_key_type active_pubkey,
                                                bool broadcast)
{
   return my->register_account( account_kind::wallet, name, owner_pubkey, active_pubkey, broadcast );
}

signed_transaction wallet_api::create_account(account_kind kind,
                                              string name,
                                              public_key_type owner_pubkey,
                                              public_key_type active_pubkey,
                                              bool broadcast)
{
   return my->register_account( kind, name, owner_pubkey, active_pubkey, broadcast );
}

signed_transaction wallet_api::register_vault_account(string name,
                                                      public_key_type owner_pubkey,
                                                      public_key_type active_pubkey,
                                                      bool broadcast)
{
   return my->register_account( account_kind::vault, name, owner_pubkey, active_pubkey, broadcast );
}

signed_transaction wallet_api::tether_accounts(string wallet,
                                               string vault,
                                               bool broadcast)
{
   return my->tether_accounts( wallet, vault, broadcast );
}

signed_transaction wallet_api::create_account_with_brain_key(string brain_key,
                                                             string account_name,
                                                             bool broadcast /* = false */)
{
   return my->create_account_with_brain_key( brain_key, account_name, broadcast );
}
signed_transaction wallet_api::issue_asset(string to_account, string amount, string symbol,
                                           string memo, bool broadcast)
{
   return my->issue_asset(to_account, amount, symbol, memo, broadcast);
}

signed_transaction wallet_api::issue_webasset(string to_account, string amount, string reserved, string unique_id, bool broadcast)
{
    return my->issue_webasset(to_account, amount, reserved, unique_id, broadcast);
}

signed_transaction wallet_api::transfer(string from, string to, string amount,
                                        string asset_symbol, string memo, bool broadcast /* = false */)
{
   return my->transfer(from, to, amount, asset_symbol, memo, broadcast);
}

signed_transaction wallet_api::transfer_vault_to_wallet(string vault, string wallet, string amount,
                                                        string asset_symbol, share_type reserved, bool broadcast /* = false */)
{
   return my->transfer_vault_to_wallet(vault, wallet, amount, asset_symbol, reserved, broadcast);
}

signed_transaction wallet_api::create_asset(string issuer,
                                            string symbol,
                                            uint8_t precision,
                                            asset_options common,
                                            fc::optional<bitasset_options> bitasset_opts,
                                            bool broadcast)

{
   return my->create_asset(issuer, symbol, precision, common, bitasset_opts, broadcast);
}

signed_transaction wallet_api::update_asset(string symbol,
                                            optional<string> new_issuer,
                                            asset_options new_options,
                                            bool broadcast /* = false */)
{
   return my->update_asset(symbol, new_issuer, new_options, broadcast);
}

signed_transaction wallet_api::update_bitasset(string symbol,
                                               bitasset_options new_options,
                                               bool broadcast /* = false */)
{
   return my->update_bitasset(symbol, new_options, broadcast);
}

signed_transaction wallet_api::update_asset_feed_producers(string symbol,
                                                           flat_set<string> new_feed_producers,
                                                           bool broadcast /* = false */)
{
   return my->update_asset_feed_producers(symbol, new_feed_producers, broadcast);
}

signed_transaction wallet_api::publish_asset_feed(string publishing_account,
                                                  string symbol,
                                                  price_feed feed,
                                                  bool broadcast /* = false */)
{
   return my->publish_asset_feed(publishing_account, symbol, feed, broadcast);
}

signed_transaction wallet_api::fund_asset_fee_pool(string from,
                                                   string symbol,
                                                   string amount,
                                                   bool broadcast /* = false */)
{
   return my->fund_asset_fee_pool(from, symbol, amount, broadcast);
}

signed_transaction wallet_api::claim_asset_accumulated_fees_pool(string symbol,
                                                                string amount,
                                                                bool broadcast /* = false */)
{
   return my->claim_asset_accumulated_fees_pool(symbol, amount, broadcast);
}

signed_transaction wallet_api::reserve_asset(string from,
                                          string amount,
                                          string symbol,
                                          bool broadcast /* = false */)
{
   return my->reserve_asset(from, amount, symbol, broadcast);
}

signed_transaction wallet_api::global_settle_asset(string symbol,
                                                   price settle_price,
                                                   bool broadcast /* = false */)
{
   return my->global_settle_asset(symbol, settle_price, broadcast);
}

signed_transaction wallet_api::settle_asset(string account_to_settle,
                                            string amount_to_settle,
                                            string symbol,
                                            bool broadcast /* = false */)
{
   return my->settle_asset(account_to_settle, amount_to_settle, symbol, broadcast);
}

signed_transaction wallet_api::whitelist_account(string authorizing_account,
                                                 string account_to_list,
                                                 account_whitelist_operation::account_listing new_listing_status,
                                                 bool broadcast /* = false */)
{
   return my->whitelist_account(authorizing_account, account_to_list, new_listing_status, broadcast);
}

signed_transaction wallet_api::create_committee_member(string owner_account, string url,
                                               bool broadcast /* = false */)
{
   return my->create_committee_member(owner_account, url, broadcast);
}

map<string,witness_id_type> wallet_api::list_witnesses(const string& lowerbound, uint32_t limit)
{
   return my->_remote_db->lookup_witness_accounts(lowerbound, limit);
}

map<string,committee_member_id_type> wallet_api::list_committee_members(const string& lowerbound, uint32_t limit)
{
   return my->_remote_db->lookup_committee_member_accounts(lowerbound, limit);
}

witness_object wallet_api::get_witness(string owner_account)
{
   return my->get_witness(owner_account);
}

committee_member_object wallet_api::get_committee_member(string owner_account)
{
   return my->get_committee_member(owner_account);
}

signed_transaction wallet_api::create_witness(string owner_account,
                                              string url,
                                              bool broadcast /* = false */)
{
   return my->create_witness(owner_account, url, broadcast);
}

signed_transaction wallet_api::create_worker(
   string owner_account,
   time_point_sec work_begin_date,
   time_point_sec work_end_date,
   share_type daily_pay,
   string name,
   string url,
   variant worker_settings,
   bool broadcast /* = false */)
{
   return my->create_worker( owner_account, work_begin_date, work_end_date,
      daily_pay, name, url, worker_settings, broadcast );
}

signed_transaction wallet_api::update_worker_votes(
   string owner_account,
   worker_vote_delta delta,
   bool broadcast /* = false */)
{
   return my->update_worker_votes( owner_account, delta, broadcast );
}

signed_transaction wallet_api::update_witness(
   string witness_name,
   string url,
   string block_signing_key,
   bool broadcast /* = false */)
{
   return my->update_witness(witness_name, url, block_signing_key, broadcast);
}

vector< vesting_balance_object_with_info > wallet_api::get_vesting_balances( string account_name )
{
   return my->get_vesting_balances( account_name );
}

signed_transaction wallet_api::withdraw_vesting(
   string witness_name,
   string amount,
   string asset_symbol,
   bool broadcast /* = false */)
{
   return my->withdraw_vesting( witness_name, amount, asset_symbol, broadcast );
}

signed_transaction wallet_api::vote_for_committee_member(string voting_account,
                                                 string witness,
                                                 bool approve,
                                                 bool broadcast /* = false */)
{
   return my->vote_for_committee_member(voting_account, witness, approve, broadcast);
}

signed_transaction wallet_api::vote_for_witness(string voting_account,
                                                string witness,
                                                bool approve,
                                                bool broadcast /* = false */)
{
   return my->vote_for_witness(voting_account, witness, approve, broadcast);
}

signed_transaction wallet_api::set_voting_proxy(string account_to_modify,
                                                optional<string> voting_account,
                                                bool broadcast /* = false */)
{
   return my->set_voting_proxy(account_to_modify, voting_account, broadcast);
}

signed_transaction wallet_api::set_desired_witness_and_committee_member_count(string account_to_modify,
                                                                      uint16_t desired_number_of_witnesses,
                                                                      uint16_t desired_number_of_committee_members,
                                                                      bool broadcast /* = false */)
{
   return my->set_desired_witness_and_committee_member_count(account_to_modify, desired_number_of_witnesses,
                                                     desired_number_of_committee_members, broadcast);
}

void wallet_api::set_wallet_filename(string wallet_filename)
{
   my->_wallet_filename = wallet_filename;
}

signed_transaction wallet_api::sign_transaction(signed_transaction tx, bool broadcast /* = false */)
{ try {

   return my->sign_transaction( tx, broadcast);

} FC_CAPTURE_AND_RETHROW( (tx) ) }

signed_transaction wallet_api::sign_transaction_with_keys(signed_transaction tx, std::vector<string> wif_keys, bool broadcast /* = false */)
{ try {

   return my->sign_transaction_with_keys(tx, wif_keys, broadcast);

} FC_CAPTURE_AND_RETHROW( (tx) ) }

operation wallet_api::get_prototype_operation(string operation_name)
{
   return my->get_prototype_operation( operation_name );
}

void wallet_api::dbg_make_uia(string creator, string symbol)
{
   FC_ASSERT(!is_locked());
   my->dbg_make_uia(creator, symbol);
}

void wallet_api::dbg_make_mia(string creator, string symbol)
{
   FC_ASSERT(!is_locked());
   my->dbg_make_mia(creator, symbol);
}

void wallet_api::dbg_push_blocks( std::string src_filename, uint32_t count )
{
   my->dbg_push_blocks( src_filename, count );
}

void wallet_api::dbg_generate_blocks( std::string debug_wif_key, uint32_t count )
{
   my->dbg_generate_blocks( debug_wif_key, count );
}

void wallet_api::dbg_stream_json_objects( const std::string& filename )
{
   my->dbg_stream_json_objects( filename );
}

void wallet_api::dbg_update_object( fc::variant_object update )
{
   my->dbg_update_object( update );
}

void wallet_api::network_add_nodes( const vector<string>& nodes )
{
   my->network_add_nodes( nodes );
}

vector< variant > wallet_api::network_get_connected_peers()
{
   return my->network_get_connected_peers();
}

void wallet_api::flood_network(string prefix, uint32_t number_of_transactions)
{
   FC_ASSERT(!is_locked());
   my->flood_network(prefix, number_of_transactions);
}

signed_transaction wallet_api::propose_parameter_change(
   const string& proposing_account,
   fc::time_point_sec expiration_time,
   const variant_object& changed_values,
   bool broadcast /* = false */
   )
{
   return my->propose_parameter_change( proposing_account, expiration_time, changed_values, broadcast );
}

signed_transaction wallet_api::propose_fee_change(
   const string& proposing_account,
   fc::time_point_sec expiration_time,
   const variant_object& changed_fees,
   bool broadcast /* = false */
   )
{
   return my->propose_fee_change( proposing_account, expiration_time, changed_fees, broadcast );
}

signed_transaction wallet_api::approve_proposal(
   const string& fee_paying_account,
   const string& proposal_id,
   const approval_delta& delta,
   bool broadcast /* = false */
   )
{
   return my->approve_proposal( fee_paying_account, proposal_id, delta, broadcast );
}

global_property_object wallet_api::get_global_properties() const
{
   return my->get_global_properties();
}

dynamic_global_property_object wallet_api::get_dynamic_global_properties() const
{
   return my->get_dynamic_global_properties();
}

string wallet_api::help()const
{
   std::vector<std::string> method_names = my->method_documentation.get_method_names();
   std::stringstream ss;
   for (const std::string method_name : method_names)
   {
      try
      {
         ss << my->method_documentation.get_brief_description(method_name);
      }
      catch (const fc::key_not_found_exception&)
      {
         ss << method_name << " (no help available)\n";
      }
   }
   return ss.str();
}

string wallet_api::gethelp(const string& method)const
{
   fc::api<wallet_api> tmp;
   std::stringstream ss;
   ss << "\n";

   if( method == "import_key" )
   {
      ss << "usage: import_key ACCOUNT_NAME_OR_ID  WIF_PRIVATE_KEY\n\n";
      ss << "example: import_key \"1.3.11\" 5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3\n";
      ss << "example: import_key \"usera\" 5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3\n";
   }
   else if( method == "transfer" )
   {
      ss << "usage: transfer FROM TO AMOUNT SYMBOL \"memo\" BROADCAST\n\n";
      ss << "example: transfer \"1.3.11\" \"1.3.4\" 1000.03 CORE \"memo\" true\n";
      ss << "example: transfer \"usera\" \"userb\" 1000.123 CORE \"memo\" true\n";
   }
   else if( method == "transfer_vault_to_wallet" )
   {
      ss << "usage: transfer_vault_to_wallet FROM_VAULT TO_WALLET AMOUNT SYMBOL RESERVED_AMOUNT BROADCAST\n\n";
      ss << "example: transfer vault-cli wallet-cli 1000 1.3.1 0 true\n";
      ss << "example: transfer 1.2.30 1.2.31 1000.123 1.3.2 0 true\n";
   }
   else if( method == "create_account_with_brain_key" )
   {
      ss << "usage: create_account_with_brain_key BRAIN_KEY ACCOUNT_NAME REGISTRAR REFERRER BROADCAST\n\n";
      ss << "example: create_account_with_brain_key \"my really long brain key\" \"newaccount\" \"1.3.11\" \"1.3.11\" true\n";
      ss << "example: create_account_with_brain_key \"my really long brain key\" \"newaccount\" \"someaccount\" \"otheraccount\" true\n";
      ss << "\n";
      ss << "This method should be used if you would like the wallet to generate new keys derived from the brain key.\n";
      ss << "The BRAIN_KEY will be used as the owner key, and the active key will be derived from the BRAIN_KEY.  Use\n";
      ss << "register_account if you already know the keys you know the public keys that you would like to register.\n";

   }
   else if( method == "register_account" )
   {
      ss << "usage: register_account ACCOUNT_NAME OWNER_PUBLIC_KEY ACTIVE_PUBLIC_KEY REGISTRAR REFERRER REFERRER_PERCENT BROADCAST\n\n";
      ss << "example: register_account \"newaccount\" \"GPH5nm5Kxgb9LAdknBYTogn4w8i2KFJxhfFHzohb2ruXSZcbgCb8z\" \"GPH5nm5Kxgb9LAdknBYTogn4w8i2KFJxhfFHzohb2ruXSZcbgCb8z\" \"1.3.11\" \"1.3.11\" 50 true\n";
      ss << "\n";
      ss << "Use this method to register an account for which you do not know the private keys.";
   }
   else if( method == "create_account" )
   {
      ss << "usage: create_account ACCOUNT_KIND ACCOUNT_NAME OWNER_PUBLIC_KEY ACTIVE_PUBLIC_KEY BROADCAST\n\n";
      ss << "example: create_account vault \"newaccount\" \"GPH5nm5Kxgb9LAdknBYTogn4w8i2KFJxhfFHzohb2ruXSZcbgCb8z\" \"GPH5nm5Kxgb9LAdknBYTogn4w8i2KFJxhfFHzohb2ruXSZcbgCb8z\" true\n";
      ss << "\n";
      ss << "Use this method to register an account for which you do not know the private keys.";
   }
   else if( method == "tether_accounts" )
   {
      ss << "usage: tether_accounts WALLET_ACCOUNT_NAME VAULT_ACCOUNT_NAME BROADCAST\n\n";
      ss << "example: tether_accounts \"wallet_account\" \"vault_account\" true\n";
      ss << "\n";
      ss << "Use this method to tether a wallet account to a vault account.";
   }
   else if( method == "transfer_cycles_from_licence_to_wallet" )
   {
      ss << "usage: transfer_cycles_from_licence_to_wallet VAULT_ACCOUNT_NAME LICENSE CYCLES_TO_TRANSFER WALLET_ACCOUNT_NAME\n\n";
      ss << "example: transfer_cycles_from_licence_to_wallet \"vault\" 1.16.15 200 \"wallet\" true\n";
      ss << "\n";
      ss << "Use this method to transfer a certain amount of cycles from a license to a wallet";
   }
   else if( method == "purchase_cycle_asset" )
   {
      ss << "usage: purchase_cycle_asset ACCOUNT_NAME AMOUNT SYMBOL FREQUENCY CYCLES_TO_RECEIVE\n\n";
      ss << "example: purchase_cycle_asset \"account\" 10 \"1.3.2\" 200 20 true\n";
      ss << "\n";
      ss << "Use this method to purchase a certain amount of cycles.";
   }
   else if( method == "calculate_cycle_price" )
   {
      ss << "usage: calculate_cycle_price AMOUNT SYMBOL\n\n";
      ss << "example: calculate_cycle_price 10 \"1.3.2\"\n";
      ss << "\n";
      ss << "Use this method to calculate the price of cycles using the current frequency.";
   }
   else if( method == "create_asset" )
   {
      ss << "usage: ISSUER SYMBOL PRECISION_DIGITS OPTIONS BITASSET_OPTIONS BROADCAST\n\n";
      ss << "PRECISION_DIGITS: the number of digits after the decimal point\n\n";
      ss << "Example value of OPTIONS: \n";
      ss << fc::json::to_pretty_string( graphene::chain::asset_options() );
      ss << "\nExample value of BITASSET_OPTIONS: \n";
      ss << fc::json::to_pretty_string( graphene::chain::bitasset_options() );
      ss << "\nBITASSET_OPTIONS may be null\n";
   }
   else
   {
      std::string doxygenHelpString = my->method_documentation.get_detailed_description(method);
      if (!doxygenHelpString.empty())
         ss << doxygenHelpString;
      else
         ss << "No help defined for method " << method << "\n";
   }

   return ss.str();
}

bool wallet_api::load_wallet_file( string wallet_filename )
{
   return my->load_wallet_file( wallet_filename );
}

void wallet_api::save_wallet_file( string wallet_filename )
{
   my->save_wallet_file( wallet_filename );
}

std::map<string,std::function<string(fc::variant,const fc::variants&)> >
wallet_api::get_result_formatters() const
{
   return my->get_result_formatters();
}

bool wallet_api::is_locked()const
{
   return my->is_locked();
}
bool wallet_api::is_new()const
{
   return my->_wallet.cipher_keys.size() == 0;
}

void wallet_api::encrypt_keys()
{
   my->encrypt_keys();
}

void wallet_api::lock()
{ try {
   FC_ASSERT( !is_locked() );
   encrypt_keys();
   for( auto key : my->_keys )
      key.second = key_to_wif(fc::ecc::private_key());
   my->_keys.clear();
   my->_checksum = fc::sha512();
   my->self.lock_changed(true);
} FC_CAPTURE_AND_RETHROW() }

void wallet_api::unlock(string password)
{ try {
   FC_ASSERT(password.size() > 0);
   auto pw = fc::sha512::hash(password.c_str(), password.size());
   vector<char> decrypted = fc::aes_decrypt(pw, my->_wallet.cipher_keys);
   auto pk = fc::raw::unpack<plain_keys>(decrypted);
   FC_ASSERT(pk.checksum == pw);
   my->_keys = std::move(pk.keys);
   my->_checksum = pk.checksum;
   my->self.lock_changed(false);
} FC_CAPTURE_AND_RETHROW() }

void wallet_api::set_password( string password )
{
   if( !is_new() )
      FC_ASSERT( !is_locked(), "The wallet must be unlocked before the password can be set" );
   my->_checksum = fc::sha512::hash( password.c_str(), password.size() );
   lock();
}

vector< signed_transaction > wallet_api::import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast )
{
   return my->import_balance( name_or_id, wif_keys, broadcast );
}

namespace detail {

vector< signed_transaction > wallet_api_impl::import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast )
{ try {
   FC_ASSERT(!is_locked());
   const dynamic_global_property_object& dpo = _remote_db->get_dynamic_global_properties();
   account_object claimer = get_account( name_or_id );
   uint32_t max_ops_per_tx = 30;

   map< address, private_key_type > keys;  // local index of address -> private key
   vector< address > addrs;
   bool has_wildcard = false;
   addrs.reserve( wif_keys.size() );
   for( const string& wif_key : wif_keys )
   {
      if( wif_key == "*" )
      {
         if( has_wildcard )
            continue;
         for( const public_key_type& pub : _wallet.extra_keys[ claimer.id ] )
         {
            addrs.push_back( pub );
            auto it = _keys.find( pub );
            if( it != _keys.end() )
            {
               fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
               FC_ASSERT( privkey );
               keys[ addrs.back() ] = *privkey;
            }
            else
            {
               wlog( "Somehow _keys has no private key for extra_keys public key ${k}", ("k", pub) );
            }
         }
         has_wildcard = true;
      }
      else
      {
         optional< private_key_type > key = wif_to_key( wif_key );
         FC_ASSERT( key.valid(), "Invalid private key" );
         fc::ecc::public_key pk = key->get_public_key();
         addrs.push_back( pk );
         keys[addrs.back()] = *key;
         // see chain/balance_evaluator.cpp
         addrs.push_back( pts_address( pk, false, 56 ) );
         keys[addrs.back()] = *key;
         addrs.push_back( pts_address( pk, true, 56 ) );
         keys[addrs.back()] = *key;
         addrs.push_back( pts_address( pk, false, 0 ) );
         keys[addrs.back()] = *key;
         addrs.push_back( pts_address( pk, true, 0 ) );
         keys[addrs.back()] = *key;
      }
   }

   vector< balance_object > balances = _remote_db->get_balance_objects( addrs );
   wdump((balances));
   addrs.clear();

   set<asset_id_type> bal_types;
   for( auto b : balances ) bal_types.insert( b.balance.asset_id );

   struct claim_tx
   {
      vector< balance_claim_operation > ops;
      set< address > addrs;
   };
   vector< claim_tx > claim_txs;

   for( const asset_id_type& a : bal_types )
   {
      balance_claim_operation op;
      op.deposit_to_account = claimer.id;
      for( const balance_object& b : balances )
      {
         if( b.balance.asset_id == a )
         {
            op.total_claimed = b.available( dpo.time );
            if( op.total_claimed.amount == 0 )
               continue;
            op.balance_to_claim = b.id;
            op.balance_owner_key = keys[b.owner].get_public_key();
            if( (claim_txs.empty()) || (claim_txs.back().ops.size() >= max_ops_per_tx) )
               claim_txs.emplace_back();
            claim_txs.back().ops.push_back(op);
            claim_txs.back().addrs.insert(b.owner);
         }
      }
   }

   vector< signed_transaction > result;

   for( const claim_tx& ctx : claim_txs )
   {
      signed_transaction tx;
      tx.operations.reserve( ctx.ops.size() );
      for( const balance_claim_operation& op : ctx.ops )
         tx.operations.emplace_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees );
      tx.validate();
      signed_transaction signed_tx = sign_transaction( tx, false );
      for( const address& addr : ctx.addrs )
         signed_tx.sign( keys[addr], _chain_id );
      // if the key for a balance object was the same as a key for the account we're importing it into,
      // we may end up with duplicate signatures, so remove those
      boost::erase(signed_tx.signatures, boost::unique<boost::return_found_end>(boost::sort(signed_tx.signatures)));
      result.push_back( signed_tx );
      if( broadcast )
         _remote_net_broadcast->broadcast_transaction(signed_tx);
   }

   return result;
} FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

}

map<public_key_type, string> wallet_api::dump_private_keys()
{
   FC_ASSERT(!is_locked());
   return my->_keys;
}

signed_transaction wallet_api::upgrade_account( string name, bool broadcast )
{
   return my->upgrade_account(name,broadcast);
}

signed_transaction wallet_api::sell_asset(string seller_account,
                                          string amount_to_sell,
                                          string symbol_to_sell,
                                          string min_to_receive,
                                          string symbol_to_receive,
                                          uint32_t expiration,
                                          bool   fill_or_kill,
                                          bool   broadcast)
{
   return my->sell_asset(seller_account, amount_to_sell, symbol_to_sell, min_to_receive,
                         symbol_to_receive, expiration, fill_or_kill, broadcast);
}

signed_transaction wallet_api::sell( string seller_account,
                                     string base,
                                     string quote,
                                     double rate,
                                     double amount,
                                     bool broadcast )
{
   return my->sell_asset( seller_account, std::to_string( amount ), base,
                          std::to_string( rate * amount ), quote, 0, false, broadcast );
}

signed_transaction wallet_api::buy( string buyer_account,
                                    string base,
                                    string quote,
                                    double rate,
                                    double amount,
                                    bool broadcast )
{
   return my->sell_asset( buyer_account, std::to_string( rate * amount ), quote,
                          std::to_string( amount ), base, 0, false, broadcast );
}

signed_transaction wallet_api::borrow_asset(string seller_name, string amount_to_sell,
                                                string asset_symbol, string amount_of_collateral, bool broadcast)
{
   FC_ASSERT(!is_locked());
   return my->borrow_asset(seller_name, amount_to_sell, asset_symbol, amount_of_collateral, broadcast);
}

signed_transaction wallet_api::cancel_order(object_id_type order_id, bool broadcast)
{
   FC_ASSERT(!is_locked());
   return my->cancel_order(order_id, broadcast);
}

memo_data wallet_api::sign_memo(string from, string to, string memo)
{
   FC_ASSERT(!is_locked());
   return my->sign_memo(from, to, memo);
}

string wallet_api::read_memo(const memo_data& memo)
{
   FC_ASSERT(!is_locked());
   return my->read_memo(memo);
}

string wallet_api::get_key_label( public_key_type key )const
{
   auto key_itr   = my->_wallet.labeled_keys.get<by_key>().find(key);
   if( key_itr != my->_wallet.labeled_keys.get<by_key>().end() )
      return key_itr->label;
   return string();
}

string wallet_api::get_private_key( public_key_type pubkey )const
{
   return key_to_wif( my->get_private_key( pubkey ) );
}

public_key_type  wallet_api::get_public_key( string label )const
{
   try { return fc::variant(label).as<public_key_type>( 1 ); } catch ( ... ){}

   auto key_itr   = my->_wallet.labeled_keys.get<by_label>().find(label);
   if( key_itr != my->_wallet.labeled_keys.get<by_label>().end() )
      return key_itr->key;
   return public_key_type();
}

bool               wallet_api::set_key_label( public_key_type key, string label )
{
   auto result = my->_wallet.labeled_keys.insert( key_label{label,key} );
   if( result.second  ) return true;

   auto key_itr   = my->_wallet.labeled_keys.get<by_key>().find(key);
   auto label_itr = my->_wallet.labeled_keys.get<by_label>().find(label);
   if( label_itr == my->_wallet.labeled_keys.get<by_label>().end() )
   {
      if( key_itr != my->_wallet.labeled_keys.get<by_key>().end() )
         return my->_wallet.labeled_keys.get<by_key>().modify( key_itr, [&]( key_label& obj ){ obj.label = label; } );
   }
   return false;
}
map<string,public_key_type> wallet_api::get_blind_accounts()const
{
   map<string,public_key_type> result;
   for( const auto& item : my->_wallet.labeled_keys )
      result[item.label] = item.key;
   return result;
}
map<string,public_key_type> wallet_api::get_my_blind_accounts()const
{
   FC_ASSERT( !is_locked() );
   map<string,public_key_type> result;
   for( const auto& item : my->_wallet.labeled_keys )
   {
      if( my->_keys.find(item.key) != my->_keys.end() )
         result[item.label] = item.key;
   }
   return result;
}

public_key_type    wallet_api::create_blind_account( string label, string brain_key  )
{
   FC_ASSERT( !is_locked() );

   auto label_itr = my->_wallet.labeled_keys.get<by_label>().find(label);
   if( label_itr !=  my->_wallet.labeled_keys.get<by_label>().end() )
      FC_ASSERT( !"Key with label already exists" );
   brain_key = fc::trim_and_normalize_spaces( brain_key );
   auto secret = fc::sha256::hash( brain_key.c_str(), brain_key.size() );
   auto priv_key = fc::ecc::private_key::regenerate( secret );
   public_key_type pub_key  = priv_key.get_public_key();

   FC_ASSERT( set_key_label( pub_key, label ) );

   my->_keys[pub_key] = graphene::utilities::key_to_wif( priv_key );

   save_wallet_file();
   return pub_key;
}

vector<asset>   wallet_api::get_blind_balances( string key_or_label )
{
   vector<asset> result;
   map<asset_id_type, share_type> balances;

   vector<commitment_type> used;

   auto pub_key = get_public_key( key_or_label );
   auto& to_asset_used_idx = my->_wallet.blind_receipts.get<by_to_asset_used>();
   auto start = to_asset_used_idx.lower_bound( std::make_tuple(pub_key,asset_id_type(0),false)  );
   auto end = to_asset_used_idx.lower_bound( std::make_tuple(pub_key,asset_id_type(uint32_t(0xffffffff)),true)  );
   while( start != end )
   {
      if( !start->used  )
      {
         auto answer = my->_remote_db->get_blinded_balances( {start->commitment()} );
         if( answer.size() )
            balances[start->amount.asset_id] += start->amount.amount;
         else
            used.push_back( start->commitment() );
      }
      ++start;
   }
   for( const auto& u : used )
   {
      auto itr = my->_wallet.blind_receipts.get<by_commitment>().find( u );
      my->_wallet.blind_receipts.modify( itr, []( blind_receipt& r ){ r.used = true; } );
   }
   for( auto item : balances )
      result.push_back( asset( item.second, item.first ) );
   return result;
}

blind_confirmation wallet_api::transfer_from_blind( string from_blind_account_key_or_label,
                                                    string to_account_id_or_name,
                                                    string amount_in,
                                                    string symbol,
                                                    bool broadcast )
{ try {
   transfer_from_blind_operation from_blind;


   auto fees  = my->_remote_db->get_global_properties().parameters.current_fees;
   fc::optional<asset_object> asset_obj = get_asset(symbol);
   FC_ASSERT(asset_obj.valid(), "Could not find asset matching ${asset}", ("asset", symbol));
   auto amount = asset_obj->amount_from_string(amount_in);

   from_blind.fee  = fees->calculate_fee( from_blind, asset_obj->options.core_exchange_rate );

   auto blind_in = asset_obj->amount_to_string( from_blind.fee + amount );


   auto conf = blind_transfer_help( from_blind_account_key_or_label,
                               from_blind_account_key_or_label,
                               blind_in, symbol, false, true/*to_temp*/ );
   FC_ASSERT( conf.outputs.size() > 0 );

   auto to_account = my->get_account( to_account_id_or_name );
   from_blind.to = to_account.id;
   from_blind.amount = amount;
   from_blind.blinding_factor = conf.outputs.back().decrypted_memo.blinding_factor;
   from_blind.inputs.push_back( {conf.outputs.back().decrypted_memo.commitment, authority() } );
   from_blind.fee  = fees->calculate_fee( from_blind, asset_obj->options.core_exchange_rate );

   idump( (from_blind) );
   conf.trx.operations.push_back(from_blind);
   ilog( "about to validate" );
   conf.trx.validate();

   if( broadcast && conf.outputs.size() == 2 ) {

       // Save the change
       blind_confirmation::output conf_output;
       blind_confirmation::output change_output = conf.outputs[0];

       // The wallet must have a private key for confirmation.to, this is used to decrypt the memo
       public_key_type from_key = get_public_key(from_blind_account_key_or_label);
       conf_output.confirmation.to = from_key;
       conf_output.confirmation.one_time_key = change_output.confirmation.one_time_key;
       conf_output.confirmation.encrypted_memo = change_output.confirmation.encrypted_memo;
       conf_output.confirmation_receipt = conf_output.confirmation;
       //try {
       receive_blind_transfer( conf_output.confirmation_receipt, from_blind_account_key_or_label, "@"+to_account.name );
       //} catch ( ... ){}
   }

   ilog( "about to broadcast" );
   conf.trx = sign_transaction( conf.trx, broadcast );

   return conf;
} FC_CAPTURE_AND_RETHROW( (from_blind_account_key_or_label)(to_account_id_or_name)(amount_in)(symbol) ) }

blind_confirmation wallet_api::blind_transfer( string from_key_or_label,
                                               string to_key_or_label,
                                               string amount_in,
                                               string symbol,
                                               bool broadcast )
{
   return blind_transfer_help( from_key_or_label, to_key_or_label, amount_in, symbol, broadcast, false );
}
blind_confirmation wallet_api::blind_transfer_help( string from_key_or_label,
                                               string to_key_or_label,
                                               string amount_in,
                                               string symbol,
                                               bool broadcast,
                                               bool to_temp )
{
   blind_confirmation confirm;
   try {

   FC_ASSERT( !is_locked() );
   public_key_type from_key = get_public_key(from_key_or_label);
   public_key_type to_key   = get_public_key(to_key_or_label);

   fc::optional<asset_object> asset_obj = get_asset(symbol);
   FC_ASSERT(asset_obj.valid(), "Could not find asset matching ${asset}", ("asset", symbol));

   blind_transfer_operation blind_tr;
   blind_tr.outputs.resize(2);

   auto fees  = my->_remote_db->get_global_properties().parameters.current_fees;

   auto amount = asset_obj->amount_from_string(amount_in);

   asset total_amount = asset_obj->amount(0);

   vector<fc::sha256> blinding_factors;

   //auto from_priv_key = my->get_private_key( from_key );

   blind_tr.fee  = fees->calculate_fee( blind_tr, asset_obj->options.core_exchange_rate );

   vector<commitment_type> used;

   auto& to_asset_used_idx = my->_wallet.blind_receipts.get<by_to_asset_used>();
   auto start = to_asset_used_idx.lower_bound( std::make_tuple(from_key,amount.asset_id,false)  );
   auto end = to_asset_used_idx.lower_bound( std::make_tuple(from_key,amount.asset_id,true)  );
   while( start != end )
   {
      auto result = my->_remote_db->get_blinded_balances( {start->commitment() } );
      if( result.size() == 0 )
      {
         used.push_back( start->commitment() );
      }
      else
      {
         blind_tr.inputs.push_back({start->commitment(), start->control_authority});
         blinding_factors.push_back( start->data.blinding_factor );
         total_amount += start->amount;

         if( total_amount >= amount + blind_tr.fee )
            break;
      }
      ++start;
   }
   for( const auto& u : used )
   {
      auto itr = my->_wallet.blind_receipts.get<by_commitment>().find( u );
      my->_wallet.blind_receipts.modify( itr, []( blind_receipt& r ){ r.used = true; } );
   }

   FC_ASSERT( total_amount >= amount+blind_tr.fee, "Insufficent Balance", ("available",total_amount)("amount",amount)("fee",blind_tr.fee) );

   auto one_time_key = fc::ecc::private_key::generate();
   auto secret       = one_time_key.get_shared_secret( to_key );
   auto child        = fc::sha256::hash( secret );
   auto nonce        = fc::sha256::hash( one_time_key.get_secret() );
   auto blind_factor = fc::sha256::hash( child );

   auto from_secret  = one_time_key.get_shared_secret( from_key );
   auto from_child   = fc::sha256::hash( from_secret );
   auto from_nonce   = fc::sha256::hash( nonce );

   auto change = total_amount - amount - blind_tr.fee;
   fc::sha256 change_blind_factor;
   fc::sha256 to_blind_factor;
   if( change.amount > 0 )
   {
      idump(("to_blind_factor")(blind_factor) );
      blinding_factors.push_back( blind_factor );
      change_blind_factor = fc::ecc::blind_sum( blinding_factors, blinding_factors.size() - 1 );
      wdump(("change_blind_factor")(change_blind_factor) );
   }
   else // change == 0
   {
      blind_tr.outputs.resize(1);
      blind_factor = fc::ecc::blind_sum( blinding_factors, blinding_factors.size() );
      idump(("to_sum_blind_factor")(blind_factor) );
      blinding_factors.push_back( blind_factor );
      idump(("nochange to_blind_factor")(blind_factor) );
   }
   fc::ecc::public_key from_pub_key = from_key;
   fc::ecc::public_key to_pub_key = to_key;

   blind_output to_out;
   to_out.owner       = to_temp ? authority() : authority( 1, public_key_type( to_pub_key.child( child ) ), 1 );
   to_out.commitment  = fc::ecc::blind( blind_factor, amount.amount.value );
   idump(("to_out.blind")(blind_factor)(to_out.commitment) );


   if( blind_tr.outputs.size() > 1 )
   {
      to_out.range_proof = fc::ecc::range_proof_sign( 0, to_out.commitment, blind_factor, nonce,  0, 0, amount.amount.value );

      blind_output change_out;
      change_out.owner       = authority( 1, public_key_type( from_pub_key.child( from_child ) ), 1 );
      change_out.commitment  = fc::ecc::blind( change_blind_factor, change.amount.value );
      change_out.range_proof = fc::ecc::range_proof_sign( 0, change_out.commitment, change_blind_factor, from_nonce,  0, 0, change.amount.value );
      blind_tr.outputs[1] = change_out;


      blind_confirmation::output conf_output;
      conf_output.label = from_key_or_label;
      conf_output.pub_key = from_key;
      conf_output.decrypted_memo.from = from_key;
      conf_output.decrypted_memo.amount = change;
      conf_output.decrypted_memo.blinding_factor = change_blind_factor;
      conf_output.decrypted_memo.commitment = change_out.commitment;
      conf_output.decrypted_memo.check   = from_secret._hash[0];
      conf_output.confirmation.one_time_key = one_time_key.get_public_key();
      conf_output.confirmation.to = from_key;
      conf_output.confirmation.encrypted_memo = fc::aes_encrypt( from_secret, fc::raw::pack( conf_output.decrypted_memo ) );
      conf_output.auth = change_out.owner;
      conf_output.confirmation_receipt = conf_output.confirmation;

      confirm.outputs.push_back( conf_output );
   }
   blind_tr.outputs[0] = to_out;

   blind_confirmation::output conf_output;
   conf_output.label = to_key_or_label;
   conf_output.pub_key = to_key;
   conf_output.decrypted_memo.from = from_key;
   conf_output.decrypted_memo.amount = amount;
   conf_output.decrypted_memo.blinding_factor = blind_factor;
   conf_output.decrypted_memo.commitment = to_out.commitment;
   conf_output.decrypted_memo.check   = secret._hash[0];
   conf_output.confirmation.one_time_key = one_time_key.get_public_key();
   conf_output.confirmation.to = to_key;
   conf_output.confirmation.encrypted_memo = fc::aes_encrypt( secret, fc::raw::pack( conf_output.decrypted_memo ) );
   conf_output.auth = to_out.owner;
   conf_output.confirmation_receipt = conf_output.confirmation;

   confirm.outputs.push_back( conf_output );

   /** commitments must be in sorted order */
   std::sort( blind_tr.outputs.begin(), blind_tr.outputs.end(),
              [&]( const blind_output& a, const blind_output& b ){ return a.commitment < b.commitment; } );
   std::sort( blind_tr.inputs.begin(), blind_tr.inputs.end(),
              [&]( const blind_input& a, const blind_input& b ){ return a.commitment < b.commitment; } );

   confirm.trx.operations.emplace_back( std::move(blind_tr) );
   ilog( "validate before" );
   confirm.trx.validate();
   confirm.trx = sign_transaction(confirm.trx, broadcast);

   if( broadcast )
   {
      for( const auto& out : confirm.outputs )
      {
         try { receive_blind_transfer( out.confirmation_receipt, from_key_or_label, "" ); } catch ( ... ){}
      }
   }

   return confirm;
} FC_CAPTURE_AND_RETHROW( (from_key_or_label)(to_key_or_label)(amount_in)(symbol)(broadcast)(confirm) ) }



/**
 *  Transfers a public balance from @from to one or more blinded balances using a
 *  stealth transfer.
 */
blind_confirmation wallet_api::transfer_to_blind( string from_account_id_or_name,
                                                  string asset_symbol,
                                                  /** map from key or label to amount */
                                                  vector<pair<string, string>> to_amounts,
                                                  bool broadcast )
{ try {
   FC_ASSERT( !is_locked() );

   blind_confirmation confirm;
   account_object from_account = my->get_account(from_account_id_or_name);

   fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
   FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

   transfer_to_blind_operation bop;
   bop.from   = from_account.id;

   vector<fc::sha256> blinding_factors;

   asset total_amount = asset_obj->amount(0);

   for( auto item : to_amounts )
   {
      auto one_time_key = fc::ecc::private_key::generate();
      auto to_key       = get_public_key( item.first );
      auto secret       = one_time_key.get_shared_secret( to_key );
      auto child        = fc::sha256::hash( secret );
      auto nonce        = fc::sha256::hash( one_time_key.get_secret() );
      auto blind_factor = fc::sha256::hash( child );

      blinding_factors.push_back( blind_factor );

      auto amount = asset_obj->amount_from_string(item.second);
      total_amount += amount;


      fc::ecc::public_key to_pub_key = to_key;
      blind_output out;
      out.owner       = authority( 1, public_key_type( to_pub_key.child( child ) ), 1 );
      out.commitment  = fc::ecc::blind( blind_factor, amount.amount.value );
      if( to_amounts.size() > 1 )
         out.range_proof = fc::ecc::range_proof_sign( 0, out.commitment, blind_factor, nonce,  0, 0, amount.amount.value );


      blind_confirmation::output conf_output;
      conf_output.label = item.first;
      conf_output.pub_key = to_key;
      conf_output.decrypted_memo.amount = amount;
      conf_output.decrypted_memo.blinding_factor = blind_factor;
      conf_output.decrypted_memo.commitment = out.commitment;
      conf_output.decrypted_memo.check   = secret._hash[0];
      conf_output.confirmation.one_time_key = one_time_key.get_public_key();
      conf_output.confirmation.to = to_key;
      conf_output.confirmation.encrypted_memo = fc::aes_encrypt( secret, fc::raw::pack( conf_output.decrypted_memo ) );
      conf_output.confirmation_receipt = conf_output.confirmation;

      confirm.outputs.push_back( conf_output );

      bop.outputs.push_back(out);
   }
   bop.amount          = total_amount;
   bop.blinding_factor = fc::ecc::blind_sum( blinding_factors, blinding_factors.size() );

   /** commitments must be in sorted order */
   std::sort( bop.outputs.begin(), bop.outputs.end(),
              [&]( const blind_output& a, const blind_output& b ){ return a.commitment < b.commitment; } );

   confirm.trx.operations.push_back( bop );
   my->set_operation_fees( confirm.trx, my->_remote_db->get_global_properties().parameters.current_fees);
   confirm.trx.validate();
   confirm.trx = sign_transaction(confirm.trx, broadcast);

   if( broadcast )
   {
      for( const auto& out : confirm.outputs )
      {
         try { receive_blind_transfer( out.confirmation_receipt, "@"+from_account.name, "from @"+from_account.name ); } catch ( ... ){}
      }
   }

   return confirm;
} FC_CAPTURE_AND_RETHROW( (from_account_id_or_name)(asset_symbol)(to_amounts) ) }

blind_receipt wallet_api::receive_blind_transfer( string confirmation_receipt, string opt_from, string opt_memo )
{
   FC_ASSERT( !is_locked() );
   stealth_confirmation conf(confirmation_receipt);
   FC_ASSERT( conf.to );

   blind_receipt result;
   result.conf = conf;

   auto to_priv_key_itr = my->_keys.find( *conf.to );
   FC_ASSERT( to_priv_key_itr != my->_keys.end(), "No private key for receiver", ("conf",conf) );


   auto to_priv_key = wif_to_key( to_priv_key_itr->second );
   FC_ASSERT( to_priv_key );

   auto secret       = to_priv_key->get_shared_secret( conf.one_time_key );
   auto child        = fc::sha256::hash( secret );

   auto child_priv_key = to_priv_key->child( child );
   //auto blind_factor = fc::sha256::hash( child );

   auto plain_memo = fc::aes_decrypt( secret, conf.encrypted_memo );
   auto memo = fc::raw::unpack<stealth_confirmation::memo_data>( plain_memo );

   result.to_key   = *conf.to;
   result.to_label = get_key_label( result.to_key );
   if( memo.from )
   {
      result.from_key = *memo.from;
      result.from_label = get_key_label( result.from_key );
      if( result.from_label == string() )
      {
         result.from_label = opt_from;
         set_key_label( result.from_key, result.from_label );
      }
   }
   else
   {
      result.from_label = opt_from;
   }
   result.amount = memo.amount;
   result.memo = opt_memo;

   // confirm the amount matches the commitment (verify the blinding factor)
   auto commtiment_test = fc::ecc::blind( memo.blinding_factor, memo.amount.amount.value );
   FC_ASSERT( fc::ecc::verify_sum( {commtiment_test}, {memo.commitment}, 0 ) );

   blind_balance bal;
   bal.amount = memo.amount;
   bal.to     = *conf.to;
   if( memo.from ) bal.from   = *memo.from;
   bal.one_time_key = conf.one_time_key;
   bal.blinding_factor = memo.blinding_factor;
   bal.commitment = memo.commitment;
   bal.used = false;

   auto child_pubkey = child_priv_key.get_public_key();
   auto owner = authority(1, public_key_type(child_pubkey), 1);
   result.control_authority = owner;
   result.data = memo;

   auto child_key_itr = owner.key_auths.find( child_pubkey );
   if( child_key_itr != owner.key_auths.end() )
      my->_keys[child_key_itr->first] = key_to_wif( child_priv_key );

   // my->_wallet.blinded_balances[memo.amount.asset_id][bal.to].push_back( bal );

   result.date = fc::time_point::now();
   my->_wallet.blind_receipts.insert( result );
   my->_keys[child_pubkey] = key_to_wif( child_priv_key );

   save_wallet_file();

   return result;
}

vector<blind_receipt> wallet_api::blind_history( string key_or_account )
{
   vector<blind_receipt> result;
   auto pub_key = get_public_key( key_or_account );

   if( pub_key == public_key_type() )
      return vector<blind_receipt>();

   for( auto& r : my->_wallet.blind_receipts )
   {
      if( r.from_key == pub_key || r.to_key == pub_key )
         result.push_back( r );
   }
   std::sort( result.begin(), result.end(), [&]( const blind_receipt& a, const blind_receipt& b ){ return a.date > b.date; } );
   return result;
}

//////////////////////////////////
//                              //
// LICENSES:                    //
//                              //
//////////////////////////////////

vector<license_type_object> wallet_api::get_license_types() const
{
   return my->_remote_db->get_license_types();
}

vector<pair<string, license_type_id_type>> wallet_api::get_license_type_names_ids() const
{
   return my->_remote_db->get_license_type_names_ids();
}

signed_transaction wallet_api::issue_license( const string& issuer, const string& account, const string& license,
                                              share_type bonus_percentage, frequency_type account_frequency,
                                              bool broadcast )
{
   return my->issue_license( issuer, account, license, bonus_percentage, account_frequency, broadcast );
}

signed_transaction wallet_api::submit_cycles_to_queue_by_license( const string& account, share_type amount, const string& license,
                                                                  frequency_type frequency, const string& comment, bool broadcast )
{
   return my->submit_cycles_to_queue_by_license( account, amount, license, frequency, comment, broadcast );
}

signed_transaction wallet_api::update_queue_parameters(
    optional<bool> enable_dascoin_queue,
    optional<uint32_t> reward_interval_time_seconds,
    optional<share_type> dascoin_reward_amount, bool broadcast) const {
  return my->update_queue_parameters(enable_dascoin_queue,
                                     reward_interval_time_seconds,
                                     dascoin_reward_amount, broadcast);
}

vector<optional<license_information_object>> wallet_api::get_license_information(const vector<account_id_type>& account_ids) const
{
   return my->_remote_db->get_license_information(account_ids);
}

optional<total_cycles_res> wallet_api::get_total_cycles() const
{
    return my->_remote_db->get_total_cycles();
}

optional<queue_projection_res> wallet_api::get_queue_projection() const
{
    return my->_remote_db->get_queue_projection();
}

acc_id_share_t_res wallet_api::get_account_cycle_balance(const string& name_or_id) const
{
   if( auto real_id = detail::maybe_id<account_id_type>(name_or_id) )
      return my->_remote_db->get_free_cycle_balance(*real_id);
   return my->_remote_db->get_free_cycle_balance(get_account(name_or_id).id);
}

acc_id_vec_cycle_agreement_res wallet_api::get_full_cycle_balances(const string& name_or_id) const
{
   if( auto real_id = detail::maybe_id<account_id_type>(name_or_id) )
      return my->_remote_db->get_all_cycle_balances(*real_id);
   return my->_remote_db->get_all_cycle_balances(get_account(name_or_id).id);
}

signed_transaction wallet_api::transfer_cycles_from_licence_to_wallet(string vault, license_type_id_type license, share_type amount_of_cycles_to_transfer, string wallet, bool broadcast)
{
    return my->transfer_cycles_from_licence_to_wallet(vault, license, amount_of_cycles_to_transfer, wallet, broadcast);
}

signed_transaction wallet_api::purchase_cycle_asset(string account, string amount_to_sell, string symbol_to_sell, double frequency, double amount_of_cycles_to_receive, bool broadcast)
{
    return my->purchase_cycle_asset(account, amount_to_sell, symbol_to_sell, frequency, amount_of_cycles_to_receive, broadcast);
}

optional<cycle_price> wallet_api::calculate_cycle_price(share_type cycle_amount, string asset_symbol_or_id) const
{
    const auto& asset_id = get_asset_id(asset_symbol_or_id);
    return my->_remote_db->calculate_cycle_price(cycle_amount, asset_id);
}

optional<withdrawal_limit> wallet_api::get_withdrawal_limit(const string& account_id_or_name, const string& asset_symbol_or_id) const
{
    return my->_remote_db->get_withdrawal_limit(get_account(account_id_or_name).id, get_asset_id(asset_symbol_or_id));
}

acc_id_share_t_res wallet_api::get_dascoin_balance(const string& name_or_id) const
{
   if( auto real_id = detail::maybe_id<account_id_type>(name_or_id) )
      return my->_remote_db->get_dascoin_balance(*real_id);
   return my->_remote_db->get_dascoin_balance(get_account(name_or_id).id);
}

uint32_t wallet_api::get_reward_queue_size() const
{
   return my->_remote_db->get_reward_queue_size();
}

signed_transaction wallet_api::wire_out(const string& account, share_type amount, bool broadcast) const
{
   return my->wire_out(account, amount, broadcast);
}

signed_transaction wallet_api::wire_out_with_fee(const string& account, share_type amount, const string& currency_of_choice,
                                                 const string& to_address, const string& memo, bool broadcast) const
{
  return my->wire_out_with_fee(account, amount, currency_of_choice, to_address, memo, broadcast);
}

signed_transaction wallet_api::set_roll_back_enabled(const string& account, bool roll_back_enabled, bool broadcast) const
{
  return my->set_roll_back_enabled(account, roll_back_enabled, broadcast);
}

signed_transaction wallet_api::roll_back_public_keys(const string& authority, const string& account,bool broadcast) const
{
  return my->roll_back_public_keys(authority, account, broadcast);
}

signed_transaction wallet_api::set_chain_authority(const string& issuer, const string& account, const string& kind, bool broadcast) const
{
  return my->set_chain_authority(issuer, account, kind, broadcast);
}

vector<issue_asset_request_object> wallet_api::get_all_webasset_issue_requests() const
{
   return my->_remote_db->get_all_webasset_issue_requests();
}

vector<wire_out_holder_object> wallet_api::get_all_wire_out_holders() const
{
   return my->_remote_db->get_all_wire_out_holders();
}

vector<wire_out_with_fee_holder_object> wallet_api::get_all_wire_out_with_fee_holders() const
{
  return my->_remote_db->get_all_wire_out_with_fee_holders();
}

vector<reward_queue_object> wallet_api::get_reward_queue() const
{
   return my->_remote_db->get_reward_queue();
}

vector<reward_queue_object> wallet_api::get_reward_queue_by_page(uint32_t from, uint32_t amount) const
{
   return my->_remote_db->get_reward_queue_by_page(from, amount);
}

acc_id_queue_subs_w_pos_res wallet_api::get_queue_submissions_with_pos(account_id_type account_id) const
{
   return my->_remote_db->get_queue_submissions_with_pos(account_id);
}

order_book wallet_api::get_order_book( const string& base, const string& quote, unsigned limit )
{
   return( my->_remote_db->get_order_book( base, quote, limit ) );
}

signed_transaction wallet_api::set_daspay_transaction_ratio(const string& authority,
                                               share_type debit_ratio,
                                               share_type credit_ratio,
                                               bool broadcast) const
{
   return my->set_daspay_transaction_ratio( authority, debit_ratio, credit_ratio, broadcast );
}

signed_transaction wallet_api::create_payment_service_provider(const string& authority,
                                               const string& payment_service_provider_account,
                                               const vector<string>& payment_service_provider_clearing_accounts,
                                               bool broadcast) const
{
   return my->create_payment_service_provider( authority, payment_service_provider_account, payment_service_provider_clearing_accounts, broadcast );
}

signed_transaction wallet_api::update_payment_service_provider(const string& authority,
                                               const string& payment_service_provider_account,
                                               const vector<string>& payment_service_provider_clearing_accounts,
                                               bool broadcast) const
{
   return my->update_payment_service_provider( authority, payment_service_provider_account, payment_service_provider_clearing_accounts, broadcast );
}

signed_transaction wallet_api::delete_payment_service_provider(const string& authority,
                                               const string& payment_service_provider_account,
                                               bool broadcast) const
{
   return my->delete_payment_service_provider( authority, payment_service_provider_account, broadcast );
}

vector<payment_service_provider_object> wallet_api::get_payment_service_providers() const
{
   return my->_remote_db->get_payment_service_providers();
}

signed_transaction wallet_api::register_daspay_authority(const string& account,
                                                         const string& payment_provider,
                                                         public_key_type daspay_public_key,
                                                         bool broadcast) const
{
   return my->register_daspay_authority( account, payment_provider, daspay_public_key, broadcast );
}

signed_transaction wallet_api::unregister_daspay_authority(const string& account,
                                                           const string& payment_provider,
                                                           bool broadcast) const
{
   return my->unregister_daspay_authority( account, payment_provider, broadcast );
}

signed_transaction wallet_api::reserve_asset_on_account(const string& account,
                                                        const string& asset_amount,
                                                        const string& asset_symbol,
                                                        bool broadcast) const
{
   return my->reserve_asset_on_account( account, asset_amount, asset_symbol, broadcast );
}

signed_transaction wallet_api::unreserve_asset_on_account(const string& account,
                                                          const string& asset_amount,
                                                          const string& asset_symbol,
                                                          bool broadcast) const
{
   return my->unreserve_asset_on_account( account, asset_amount, asset_symbol, broadcast );
}

signed_transaction wallet_api::daspay_debit_account(const string& payment_service_provider_account,
                                                    const public_key_type& auth_key,
                                                    const string& user_account,
                                                    const string& asset_amount,
                                                    const string& asset_symbol,
                                                    const string& clearing_account,
                                                    const string& transaction_id,
                                                    optional<string> details,
                                                    bool broadcast) const
{
   return my->daspay_debit_account( payment_service_provider_account, auth_key, user_account, asset_amount, asset_symbol, clearing_account, transaction_id, details, broadcast );
}

signed_transaction wallet_api::daspay_credit_account(const string& payment_service_provider_account,
                                                     const string& user_account,
                                                     const string& asset_amount,
                                                     const string& asset_symbol,
                                                     const string& clearing_account,
                                                     const string& transaction_id,
                                                     optional<string> details,
                                                     bool broadcast) const
{
   return my->daspay_credit_account( payment_service_provider_account, user_account, asset_amount, asset_symbol, clearing_account, transaction_id, details, broadcast );
}

optional<vector<daspay_authority>> wallet_api::get_daspay_authority_for_account(const string& account) const
{
    account_object account_obj = my->get_account(account);
    return my->_remote_db->get_daspay_authority_for_account(account_obj.id);
}

signed_transaction wallet_api::update_daspay_clearing_parameters(const string& authority,
                                                                 optional<bool> clearing_enabled,
                                                                 optional<uint32_t> clearing_interval_time_seconds,
                                                                 optional<share_type> collateral_dascoin,
                                                                 optional<share_type> collateral_webeur,
                                                                 bool broadcast) const
{
  return my->update_daspay_clearing_parameters(authority,
                                               clearing_enabled,
                                               clearing_interval_time_seconds,
                                               collateral_dascoin,
                                               collateral_webeur,
                                               broadcast);
}

signed_transaction wallet_api::daspay_set_use_external_token_price(const string& authority,
                                                                   flat_set<asset_id_type> use_external_token_price,
                                                                   bool broadcast) const
{
    return my->daspay_set_use_external_token_price(authority, use_external_token_price, broadcast);
}

signed_transaction wallet_api::das33_pledge_asset(const string& account,
                                                  const string& amount,
                                                  const string& symbol,
                                                  optional<license_type_id_type> license,
                                                  das33_project_id_type project,
                                                  bool broadcast) const
{
   return my->das33_pledge_asset( account, amount, symbol, license, project, broadcast );
}

signed_transaction wallet_api::das33_pledge_reject(const string& authority,
                                                   const string& pledge_id,
                                                   bool broadcast) const
{
   return my->das33_pledge_reject( authority, pledge_id, broadcast );
}

signed_transaction wallet_api::das33_distribute_pledge(const string& authority,
                                                       const string& pledge_id,
                                                       share_type to_escrow,
                                                       share_type base_to_pledger,
                                                       share_type bonus_to_pledger,
                                                       bool broadcast) const
{
   return my->das33_distribute_pledge(authority, pledge_id, to_escrow, base_to_pledger, bonus_to_pledger, broadcast);
}

signed_transaction wallet_api::das33_project_reject(const string& authority,
                                                    const string& project_id,
                                                    bool broadcast) const
{
   return my->das33_pledge_reject( authority, project_id, broadcast );
}

signed_transaction wallet_api::das33_distribute_project_pledges(const string& authority,
                                                                const string& project_id,
                                                                optional<share_type> phase_number,
                                                                share_type to_escrow,
                                                                share_type base_to_pledger,
                                                                share_type bonus_to_pledger,
                                                                bool broadcast) const
{
   return my->das33_distribute_project_pledges(authority, project_id, phase_number, to_escrow, base_to_pledger, bonus_to_pledger, broadcast);
}

signed_transaction wallet_api::das33_set_use_external_btc_price(const string& authority,
                                                                bool use_exteranl_btc_price,
                                                                bool broadcast) const
{
  return my->das33_set_use_external_btc_price(authority, use_exteranl_btc_price, broadcast);
}

signed_transaction wallet_api::das33_set_use_market_token_price(const string& authority,
                                                                flat_set<asset_id_type> use_market_price_for_token,
                                                                bool broadcast) const
{
  return my->das33_set_use_market_token_price(authority, use_market_price_for_token, broadcast);
}

vector<das33_pledge_holder_object> wallet_api::get_das33_pledges(das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const
{
    return my->_remote_db->get_das33_pledges(from, limit, phase);
}

das33_pledges_by_account_result wallet_api::get_das33_pledges_by_account(const string& account) const
{
   account_object account_obj = my->get_account(account);
   return my->_remote_db->get_das33_pledges_by_account(account_obj.id);
}

vector<das33_pledge_holder_object> wallet_api::get_das33_pledges_by_project(const string& project, das33_pledge_holder_id_type from, uint32_t limit, optional<uint32_t> phase) const
{
  das33_project_object project_obj = my->_remote_db->get_das33_projects(project, 1)[0];
    return my->_remote_db->get_das33_pledges_by_project(project_obj.id, from, limit, phase);
}


vector<das33_project_object> wallet_api::get_das33_projects(const string& lower_bound_name, uint32_t limit) const
{
    return my->_remote_db->get_das33_projects(lower_bound_name, limit);
}

vector<asset> wallet_api::get_amount_of_assets_pledged_to_project(das33_project_id_type project) const
{
  return my->_remote_db->get_amount_of_assets_pledged_to_project(project);
}

vector<asset> wallet_api::get_amount_of_assets_pledged_to_project_in_phase(das33_project_id_type project, uint32_t phase) const
{
    return my->_remote_db->get_amount_of_assets_pledged_to_project_in_phase(project, phase);
}

das33_project_tokens_amount wallet_api::get_amount_of_project_tokens_received_for_asset(das33_project_id_type project, asset to_pledge) const
{
  return my->_remote_db->get_amount_of_project_tokens_received_for_asset(project, to_pledge);
}

das33_project_tokens_amount wallet_api::get_amount_of_asset_needed_for_project_token(das33_project_id_type project, asset_id_type asset_id, asset tokens) const
{
  return my->_remote_db->get_amount_of_asset_needed_for_project_token(project, asset_id, tokens);
}

vector<last_price_object> wallet_api::get_last_prices() const
{
  return my->_remote_db->get_last_prices();
}

vector<external_price_object> wallet_api::get_external_prices() const
{
  return my->_remote_db->get_external_prices();
}

signed_transaction wallet_api::create_das33_project(const string& authority,
                                                    const string& name,
                                                    const string& owner,
                                                    const string& token,
                                                    vector<pair<string, share_type>> discounts,
                                                    share_type goal_amount,
                                                    share_type min_pledge,
                                                    share_type max_pledge,
                                                    bool broadcast) const
{
    return my->create_das33_project(authority,
                                    name,
                                    owner,
                                    token,
                                    discounts,
                                    goal_amount,
                                    min_pledge,
                                    max_pledge,
                                    broadcast);
}

signed_transaction wallet_api::update_das33_project(const string& authority,
                                                    const string& project_id,
                                                    optional<string> name,
                                                    optional<string> owner,
                                                    optional<share_type> goal_amount,
                                                    optional<price> token_price,
                                                    optional<vector<pair<string, share_type>>> discounts,
                                                    optional<share_type> min_pledge,
                                                    optional<share_type> max_pledge,
                                                    optional<share_type> phase_limit,
                                                    optional<time_point_sec> phase_end,
                                                    optional<uint8_t> status,
                                                    bool broadcast) const
{
  return my->update_das33_project(authority,
                                  project_id,
                                  name,
                                  owner,
                                  goal_amount,
                                  token_price,
                                  discounts,
                                  min_pledge,
                                  max_pledge,
                                  phase_limit,
                                  phase_end,
                                  status,
                                  broadcast);
}

signed_transaction wallet_api::delete_das33_project(const string& authority,
                                        const string& project_id,
                                        bool broadcast) const
{
  return my->delete_das33_project(authority,
                                  project_id,
                                  broadcast);
}

signed_transaction wallet_api::update_delayed_operations_resolver_parameters(const string& authority, optional<bool> delayed_operations_resolver_enabled,
                                                                 optional<uint32_t> delayed_operations_resolver_interval_time_seconds,
                                                                 bool broadcast) const
{
  return my->update_delayed_operations_resolver_parameters(authority, delayed_operations_resolver_enabled,
                                               delayed_operations_resolver_interval_time_seconds,
                                               broadcast);
}

vector<delayed_operation_object> wallet_api::get_delayed_operations_for_account(account_id_type account) const
{
  return my->_remote_db->get_delayed_operations_for_account(account);
}

signed_transaction wallet_api::update_global_parameters(const string& authority,
                                                        const variant_object& changed_values,
                                                        bool broadcast) const
{
   return my->update_global_parameters( authority, changed_values, broadcast );
}

signed_transaction wallet_api::change_operation_fee(const string& authority,
                                                    share_type new_fee,
                                                    unsigned op_num,
                                                    string comment,
                                                    bool broadcast) const
{
    return my->change_operation_fee(authority, new_fee, op_num, comment, broadcast);
}

signed_transaction wallet_api::update_external_btc_price(const string& btc_issuer,
                                                         price new_price,
                                                         bool broadcast) const
{
    return my->update_external_btc_price(btc_issuer, new_price, broadcast);
}

signed_transaction wallet_api::update_external_token_price(const string& token_issuer,
                                                           asset_id_type token_id,
                                                           price new_price,
                                                           bool broadcast) const
{
  return my->update_external_token_price(token_issuer, token_id, new_price, broadcast);
}

signed_transaction wallet_api::set_active_authorities(const string& account_id_or_name,
                                                      vector<public_key_type> keys,
                                                      bool broadcast) const
{
  return my->set_active_authorities(account_id_or_name, keys, broadcast);
}

signed_block_with_info::signed_block_with_info( const signed_block& block )
   : signed_block( block )
{
   block_id = id();
   signing_key = signee();
   transaction_ids.reserve( transactions.size() );
   for( const processed_transaction& tx : transactions )
      transaction_ids.push_back( tx.id() );
}

vesting_balance_object_with_info::vesting_balance_object_with_info( const vesting_balance_object& vbo, fc::time_point_sec now )
   : vesting_balance_object( vbo )
{
   allowed_withdraw = get_allowed_withdraw( now );
   allowed_withdraw_time = now;
}

} } // graphene::wallet

namespace fc {
  void to_variant( const account_multi_index_type& accts, variant& vo, uint32_t max_depth )
  {
    to_variant( std::vector<account_object>(accts.begin(), accts.end()), vo, max_depth );
  }

  void from_variant( const variant& var, account_multi_index_type& vo, uint32_t max_depth )
  {
    const std::vector<account_object>& v = var.as<std::vector<account_object>>( max_depth );
    vo = account_multi_index_type(v.begin(), v.end());
  }
}
