#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace market {

using Money = std::int64_t;
using Quantity = std::int64_t;
inline constexpr Money maxMoney = 1'000'000'000'000LL;
inline constexpr Quantity maxQuantity = 1'000'000'000LL;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Role { Admin, Cashier };
enum class Permission { ManageUsers, ManageProducts, Purchase, Sell, Inventory, Reports, Backup };

struct Account {
    std::string username;
    std::string passwordHash;
    Role role = Role::Cashier;
    bool enabled = true;
};

struct ProductDraft {
    std::string barcode;
    std::string name;
    std::string category;
    Money purchasePrice = 0;
    Money salePrice = 0;
    Quantity minStock = 0;
};

struct Product {
    std::string productId;
    std::string barcode;
    std::string name;
    std::string category;
    Money purchasePrice = 0;
    Money salePrice = 0;
    Quantity stockQuantity = 0;
    Quantity minStock = 0;
    bool active = true;
};

struct PurchaseRecord {
    std::string id;
    std::string timestamp;
    std::string operatorName;
    std::string productId;
    std::string productName;
    Quantity quantity = 0;
    Money unitPrice = 0;
    Money totalCost = 0;
};

struct CartItem {
    std::string productId;
    Quantity quantity = 0;
};

struct SaleLine {
    std::string productId;
    std::string productName;
    Quantity quantity = 0;
    Money unitPrice = 0;
    Money unitCost = 0;
};

struct SaleRecord {
    std::string id;
    std::string timestamp;
    std::string operatorName;
    std::vector<SaleLine> items;
    Money total = 0;
    Money paid = 0;
    Money change = 0;
};

struct StocktakeRecord {
    std::string id;
    std::string timestamp;
    std::string operatorName;
    std::string productId;
    std::string productName;
    Quantity beforeQuantity = 0;
    Quantity countedQuantity = 0;
    std::string reason;
};

struct SalesSummary {
    std::size_t transactionCount = 0;
    Quantity quantity = 0;
    Money revenue = 0;
    Money cost = 0;
    Money grossProfit = 0;
};

struct PurchaseSummary {
    std::size_t transactionCount = 0;
    Quantity quantity = 0;
    Money totalCost = 0;
};

struct InventorySummary {
    std::size_t activeProducts = 0;
    Quantity totalQuantity = 0;
    Money costValue = 0;
    Money retailValue = 0;
    std::size_t lowStockCount = 0;
};

Money parseMoney(std::string_view text);
std::string formatMoney(Money value);
std::int64_t parseInteger(std::string_view text, std::int64_t minimum, std::int64_t maximum);
Money checkedAdd(Money left, Money right);
Money checkedMultiply(Money price, Quantity quantity);
void requireText(std::string_view text, std::string_view label, std::size_t maximum = 120);
void validateDate(std::string_view date);
void validateDateRange(std::string_view start, std::string_view end);
bool inDateRange(std::string_view timestamp, std::string_view start, std::string_view end);
std::string nowTimestamp();
std::string makeId(char prefix, std::uint64_t& counter);
std::string roleName(Role role);
std::string hashPassword(std::string_view password);
bool verifyPassword(std::string_view password, std::string_view encoded);
bool validPasswordHash(std::string_view encoded);

}
