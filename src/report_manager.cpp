#include "supermarket/services.hpp"

namespace market {

ReportManager::ReportManager(FileManager& files, User& user) : files_(files), user_(user) {}

SalesSummary ReportManager::salesReport(const std::string& startDate, const std::string& endDate) const {
    user_.requirePermission(Permission::Reports);
    validateDateRange(startDate, endDate);
    SalesSummary result;
    for (const auto& record : files_.data().sales) {
        if (!inDateRange(record.timestamp, startDate, endDate)) {
            continue;
        }
        ++result.transactionCount;
        result.revenue = checkedAdd(result.revenue, record.total);
        for (const auto& line : record.items) {
            result.quantity = checkedAdd(result.quantity, line.quantity);
            result.cost = checkedAdd(result.cost, checkedMultiply(line.unitCost, line.quantity));
        }
    }
    result.grossProfit = result.revenue - result.cost;
    return result;
}

PurchaseSummary ReportManager::purchaseReport(const std::string& startDate, const std::string& endDate) const {
    user_.requirePermission(Permission::Reports);
    validateDateRange(startDate, endDate);
    PurchaseSummary result;
    for (const auto& record : files_.data().purchases) {
        if (inDateRange(record.timestamp, startDate, endDate)) {
            ++result.transactionCount;
            result.quantity = checkedAdd(result.quantity, record.quantity);
            result.totalCost = checkedAdd(result.totalCost, record.totalCost);
        }
    }
    return result;
}

InventorySummary ReportManager::inventoryReport() const {
    user_.requirePermission(Permission::Reports);
    InventorySummary result;
    for (const auto& product : files_.data().products.entries()) {
        if (!product.active) {
            continue;
        }
        ++result.activeProducts;
        result.totalQuantity = checkedAdd(result.totalQuantity, product.stockQuantity);
        result.costValue = checkedAdd(result.costValue, checkedMultiply(product.purchasePrice, product.stockQuantity));
        result.retailValue = checkedAdd(result.retailValue, checkedMultiply(product.salePrice, product.stockQuantity));
        if (product.stockQuantity <= product.minStock) {
            ++result.lowStockCount;
        }
    }
    return result;
}

}
