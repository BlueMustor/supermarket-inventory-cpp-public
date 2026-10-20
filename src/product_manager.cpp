#include "supermarket/services.hpp"

#include <algorithm>

namespace market {
namespace {

void validateDraft(const ProductDraft& draft) {
    requireText(draft.barcode, "条码", 64);
    requireText(draft.name, "商品名称", 120);
    requireText(draft.category, "商品分类", 60);
    if (draft.barcode.front() == 'P' && draft.barcode.size() > 1 &&
        std::all_of(draft.barcode.begin() + 1, draft.barcode.end(), [](unsigned char ch) {
            return ch >= '0' && ch <= '9';
        })) {
        throw Error("P 加纯数字保留作商品编号，不能用作条码。");
    }
    if (draft.purchasePrice < 0 || draft.purchasePrice > maxMoney ||
        draft.salePrice < 0 || draft.salePrice > maxMoney ||
        draft.minStock < 0 || draft.minStock > maxQuantity) {
        throw Error("价格或最低库存超出允许范围。");
    }
}

void requireUniqueBarcode(const State& state, const std::string& barcode, const std::string& exceptId = "") {
    for (const auto& product : state.products.entries()) {
        if (product.barcode == barcode && product.productId != exceptId) {
            throw Error("条码已被其他商品使用，停用商品的条码也不能重复。");
        }
    }
}

void applyDraft(Product& product, const ProductDraft& draft) {
    product.barcode = draft.barcode;
    product.name = draft.name;
    product.category = draft.category;
    product.purchasePrice = draft.purchasePrice;
    product.salePrice = draft.salePrice;
    product.minStock = draft.minStock;
}

}

ProductManager::ProductManager(FileManager& files, User& user) : files_(files), user_(user) {}

std::string ProductManager::addProduct(const ProductDraft& draft) {
    user_.requirePermission(Permission::ManageProducts);
    validateDraft(draft);
    requireUniqueBarcode(files_.data(), draft.barcode);
    State next = files_.data();
    Product product;
    product.productId = makeId('P', next.nextProductId);
    applyDraft(product, draft);
    const std::string id = product.productId;
    next.products.insert(std::move(product));
    files_.commit(std::move(next));
    return id;
}

void ProductManager::updateProduct(const std::string& id, const ProductDraft& draft) {
    user_.requirePermission(Permission::ManageProducts);
    validateDraft(draft);
    const Product existing = findProduct(id);
    if (!existing.active) {
        throw Error("停用商品不能修改，请新增不同条码的商品。");
    }
    requireUniqueBarcode(files_.data(), draft.barcode, existing.productId);
    State next = files_.data();
    auto* product = next.products.find(existing.productId);
    applyDraft(*product, draft);
    files_.commit(std::move(next));
}

void ProductManager::deleteProduct(const std::string& id) {
    user_.requirePermission(Permission::ManageProducts);
    const Product existing = findProduct(id);
    if (!existing.active) {
        throw Error("商品已经停用。");
    }
    if (existing.stockQuantity != 0) {
        throw Error("库存为零时才能停用商品，请先核实并处理实物库存。");
    }
    State next = files_.data();
    next.products.find(existing.productId)->active = false;
    files_.commit(std::move(next));
}

Product ProductManager::findProduct(const std::string& idOrBarcode) const {
    user_.current();
    const auto* byId = files_.data().products.find(idOrBarcode);
    if (byId != nullptr) {
        if (!byId->active && user_.current().role != Role::Admin) {
            throw Error("商品不存在或已停用。");
        }
        return *byId;
    }
    for (const auto& product : files_.data().products.entries()) {
        if (product.barcode == idOrBarcode && (product.active || user_.current().role == Role::Admin)) {
            return product;
        }
    }
    throw Error("商品不存在或已停用。");
}

std::vector<Product> ProductManager::listProducts(const std::string& keyword, bool includeInactive) const {
    user_.current();
    if (includeInactive) {
        user_.requirePermission(Permission::ManageProducts);
    }
    std::vector<Product> result;
    for (const auto& product : files_.data().products.entries()) {
        const bool matches = keyword.empty() || product.productId.find(keyword) != std::string::npos ||
                             product.barcode.find(keyword) != std::string::npos ||
                             product.name.find(keyword) != std::string::npos ||
                             product.category.find(keyword) != std::string::npos;
        if ((product.active || includeInactive) && matches) {
            result.push_back(product);
        }
    }
    return result;
}

}
