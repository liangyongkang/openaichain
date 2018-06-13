/**
 *  @file
 *  @copyright defined in oac/LICENSE.txt
 */
#pragma once
#include <oac/chain/exceptions.hpp>
#include <oac/chain/types.hpp>
#include <oac/chain/symbol.hpp>

namespace oac { namespace chain {

/**

asset includes amount and currency symbol

asset::from_string takes a string of the form "10.0000 CUR" and constructs an asset 
with amount = 10 and symbol(4,"CUR")

*/

struct asset
{
   static constexpr int64_t max_amount = (1LL << 62) - 1;

   explicit asset(share_type a = 0, symbol id = symbol(CORE_SYMBOL)) :amount(a), sym(id) {
      OAC_ASSERT( is_amount_within_range(), asset_type_exception, "magnitude of asset amount must be less than 2^62" );
      OAC_ASSERT( sym.valid(), asset_type_exception, "invalid symbol" );
   }

   bool is_amount_within_range()const { return -max_amount <= amount && amount <= max_amount; }
   bool is_valid()const               { return is_amount_within_range() && sym.valid(); }

   double to_real()const { return static_cast<double>(amount) / precision(); }

   uint8_t     decimals()const;
   string      symbol_name()const;
   int64_t     precision()const;
   const symbol& get_symbol() const { return sym; }
   share_type get_amount()const { return amount; }

   static asset from_string(const string& from);
   string       to_string()const;

   asset& operator += (const asset& o)
   {
      FC_ASSERT(get_symbol() == o.get_symbol());
      amount += o.amount;
      return *this;
   }

   asset& operator -= (const asset& o)
   {
      FC_ASSERT(get_symbol() == o.get_symbol());
      amount -= o.amount;
      return *this;
   }
   asset operator -()const { return asset(-amount, get_symbol()); }

   friend bool operator == (const asset& a, const asset& b)
   {
      return std::tie(a.get_symbol(), a.amount) == std::tie(b.get_symbol(), b.amount);
   }
   friend bool operator < (const asset& a, const asset& b)
   {
      FC_ASSERT(a.get_symbol() == b.get_symbol());
      return std::tie(a.amount,a.get_symbol()) < std::tie(b.amount,b.get_symbol());
   }
   friend bool operator <= (const asset& a, const asset& b) { return (a == b) || (a < b); }
   friend bool operator != (const asset& a, const asset& b) { return !(a == b); }
   friend bool operator > (const asset& a, const asset& b)  { return !(a <= b); }
   friend bool operator >= (const asset& a, const asset& b) { return !(a < b);  }

   friend asset operator - (const asset& a, const asset& b) {
      FC_ASSERT(a.get_symbol() == b.get_symbol());
      return asset(a.amount - b.amount, a.get_symbol());
   }

   friend asset operator + (const asset& a, const asset& b) {
      FC_ASSERT(a.get_symbol() == b.get_symbol());
      return asset(a.amount + b.amount, a.get_symbol());
   }

   friend std::ostream& operator << (std::ostream& out, const asset& a) { return out << a.to_string(); }

   friend struct fc::reflector<asset>;

   void reflector_verify()const {
      OAC_ASSERT( is_amount_within_range(), asset_type_exception, "magnitude of asset amount must be less than 2^62" );
      OAC_ASSERT( sym.valid(), asset_type_exception, "invalid symbol" );
   }

private:
   share_type amount;
   symbol     sym;

};

struct extended_asset  {
  extended_asset(){}
  extended_asset( asset a, name n ):quantity(a),contract(n){}
  asset quantity;
  name contract;
};

bool  operator <  (const asset& a, const asset& b);
bool  operator <= (const asset& a, const asset& b);

}} // namespace oac::chain

namespace fc {
inline void to_variant(const oac::chain::asset& var, fc::variant& vo) { vo = var.to_string(); }
inline void from_variant(const fc::variant& var, oac::chain::asset& vo) {
   vo = oac::chain::asset::from_string(var.get_string());
}
}

FC_REFLECT(oac::chain::asset, (amount)(sym))
FC_REFLECT(oac::chain::extended_asset, (quantity)(contract) )