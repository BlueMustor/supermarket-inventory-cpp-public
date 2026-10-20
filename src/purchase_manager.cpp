#include "supermarket/services.hpp"

namespace market {

PurchaseManager::PurchaseManager(FileManager& files, User& user, ProductManager& products)
    : files_(files), user_(user), products_(products) {}

PurchaseRecord PurchaseManager::confirmInbound(const std::string& productId, Quantity quantity, Money unitPrice) {
    user_.requirePermission(Permission::Purchase);
    if (quantity <= 0 || quantity > maxQuantity) {
        throw Error("入库数量必须为正整数且不超过十亿。");
    }
    const Money total = checkedMultiply(unitPrice, quantity);
    const Product existing = products_.findProduct(productId);
    if (!existing.active) {
        throw Error("停用商品不能采购入库。");
    }
    if (quantity > maxQuantity - existing.stockQuantity) {
        throw Error("入库后的库存超出允许范围。");
    }
    State next = files_.data();
    PurchaseRecord record;
    record.id = makeId('I', next.nextRecordId);
    record.timestamp = nowTimestamp();
    record.operatorName = user_.current().username;
    record.productId = existing.productId;
    record.productName = existing.name;
    record.quantity = quantity;
    record.unitPrice = unitPrice;
    record.totalCost = total;
    auto* product = next.products.find(existing.productId);
    product->stockQuantity += quantity;
    product->purchasePrice = unitPrice;
    next.purchases.push_back(record);
    files_.commit(std::move(next));
    return record;
}

std::vector<PurchaseRecord> PurchaseManager::queryPurchase(const std::string& startDate, const std::string& endDate) const {
    user_.requirePermission(Permission::Purchase);
    validateDateRange(startDate, endDate);
    std::vector<PurchaseRecord> result;
    for (const auto& record : files_.data().purchases) {
        if (inDateRange(record.timestamp, startDate, endDate)) {
            result.push_back(record);
        }
    }
    return result;
}

}
