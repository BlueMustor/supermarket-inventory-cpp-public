#include "supermarket/services.hpp"

#include <algorithm>
#include <set>

namespace market {

SaleManager::SaleManager(FileManager& files, User& user, ProductManager& products)
    : files_(files), user_(user), products_(products) {}

void SaleManager::addToCart(std::vector<CartItem>& cart, const std::string& productId, Quantity quantity) const {
    user_.requirePermission(Permission::Sell);
    if (quantity <= 0 || quantity > maxQuantity) {
        throw Error("购买数量必须为正整数且不超过十亿。");
    }
    if (!cart.empty()) {
        quote(cart);
    }
    const Product product = products_.findProduct(productId);
    std::vector<CartItem> next = cart;
    auto found = std::find_if(next.begin(), next.end(), [&](const CartItem& item) {
        return item.productId == product.productId;
    });
    if (found == next.end()) {
        next.push_back({product.productId, quantity});
    } else {
        if (quantity > maxQuantity - found->quantity) {
            throw Error("购物车数量超出允许范围。");
        }
        found->quantity += quantity;
    }
    quote(next);
    cart.swap(next);
}

void SaleManager::removeFromCart(std::vector<CartItem>& cart, const std::string& productId) const {
    user_.requirePermission(Permission::Sell);
    const std::string canonicalId = products_.findProduct(productId).productId;
    const auto found = std::find_if(cart.begin(), cart.end(), [&](const CartItem& item) {
        return item.productId == canonicalId;
    });
    if (found == cart.end()) {
        throw Error("购物车中没有该商品。");
    }
    cart.erase(found);
}

Money SaleManager::quote(const std::vector<CartItem>& cart) const {
    user_.requirePermission(Permission::Sell);
    if (cart.empty()) {
        throw Error("购物车为空。");
    }
    Money total = 0;
    std::set<std::string> seen;
    for (const auto& item : cart) {
        const Product product = products_.findProduct(item.productId);
        if (!product.active || item.productId != product.productId || !seen.insert(product.productId).second) {
            throw Error("购物车包含停用商品、非规范编号或重复行。");
        }
        if (item.quantity <= 0 || item.quantity > product.stockQuantity) {
            throw Error("商品 " + product.name + " 的库存不足或购买数量不合法。");
        }
        total = checkedAdd(total, checkedMultiply(product.salePrice, item.quantity));
    }
    return total;
}

SaleRecord SaleManager::checkout(std::vector<CartItem>& cart, Money paid) {
    user_.requirePermission(Permission::Sell);
    const Money total = quote(cart);
    if (paid < total || paid > maxMoney) {
        throw Error("实收金额不足或超出允许范围。");
    }
    State next = files_.data();
    SaleRecord record;
    record.id = makeId('S', next.nextRecordId);
    record.timestamp = nowTimestamp();
    record.operatorName = user_.current().username;
    record.total = total;
    record.paid = paid;
    record.change = paid - total;
    Money totalCost = 0;
    for (const auto& item : cart) {
        auto* product = next.products.find(item.productId);
        totalCost = checkedAdd(totalCost, checkedMultiply(product->purchasePrice, item.quantity));
        record.items.push_back({product->productId, product->name, item.quantity,
                                product->salePrice, product->purchasePrice});
        product->stockQuantity -= item.quantity;
    }
    next.sales.push_back(record);
    files_.commit(std::move(next));
    cart.clear();
    return record;
}

std::vector<SaleRecord> SaleManager::querySale(const std::string& startDate, const std::string& endDate) const {
    user_.requirePermission(Permission::Sell);
    validateDateRange(startDate, endDate);
    const Account account = user_.current();
    std::vector<SaleRecord> result;
    for (const auto& record : files_.data().sales) {
        if (inDateRange(record.timestamp, startDate, endDate) &&
            (account.role == Role::Admin || record.operatorName == account.username)) {
            result.push_back(record);
        }
    }
    return result;
}

}
