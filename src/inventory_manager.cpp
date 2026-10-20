#include "supermarket/services.hpp"

namespace market {

InventoryManager::InventoryManager(FileManager& files, User& user, ProductManager& products)
    : files_(files), user_(user), products_(products) {}

StocktakeRecord InventoryManager::countInventory(const std::string& productId, Quantity counted, const std::string& reason) {
    user_.requirePermission(Permission::Inventory);
    if (counted < 0 || counted > maxQuantity) {
        throw Error("实盘数量必须在零至十亿之间。");
    }
    requireText(reason, "盘点原因", 240);
    const Product existing = products_.findProduct(productId);
    if (!existing.active) {
        throw Error("停用商品不能盘点。");
    }
    State next = files_.data();
    StocktakeRecord record;
    record.id = makeId('T', next.nextRecordId);
    record.timestamp = nowTimestamp();
    record.operatorName = user_.current().username;
    record.productId = existing.productId;
    record.productName = existing.name;
    record.beforeQuantity = existing.stockQuantity;
    record.countedQuantity = counted;
    record.reason = reason;
    next.products.find(existing.productId)->stockQuantity = counted;
    next.stocktakes.push_back(record);
    files_.commit(std::move(next));
    return record;
}

std::vector<Product> InventoryManager::checkLowStock() const {
    user_.requirePermission(Permission::Inventory);
    std::vector<Product> result;
    for (const auto& product : files_.data().products.entries()) {
        if (product.active && product.stockQuantity <= product.minStock) {
            result.push_back(product);
        }
    }
    return result;
}

std::vector<StocktakeRecord> InventoryManager::records() const {
    user_.requirePermission(Permission::Inventory);
    return files_.data().stocktakes;
}

}
