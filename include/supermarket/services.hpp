#pragma once

#include "supermarket/product_table.hpp"
#include "supermarket/console_ui.hpp"

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>

namespace market {

struct State {
    ProductTable products;
    std::vector<Account> users;
    std::vector<PurchaseRecord> purchases;
    std::vector<SaleRecord> sales;
    std::vector<StocktakeRecord> stocktakes;
    std::uint64_t nextProductId = 1;
    std::uint64_t nextRecordId = 1;
};

class User;

class FileManager {
public:
    explicit FileManager(std::filesystem::path dataDirectory);
    ~FileManager();
    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    const State& data() const noexcept;
    const std::filesystem::path& directory() const noexcept;
    void commit(State next);
    std::filesystem::path backupData(const User& user) const;

private:
    struct Lock;
    void loadData();
    static void validate(const State& state);
    static void writeSnapshot(const std::filesystem::path& path, const State& state);
    static State readSnapshot(const std::filesystem::path& path);

    std::filesystem::path dataDirectory_;
    std::unique_ptr<Lock> lock_;
    State state_;
};

class User {
public:
    explicit User(FileManager& files);
    bool hasUsers() const noexcept;
    bool isLoggedIn() const noexcept;
    void bootstrapAdmin(const std::string& username, const std::string& password);
    void login(const std::string& username, const std::string& password);
    void logout() noexcept;
    const Account& current() const;
    bool checkPermission(Permission permission) const;
    void requirePermission(Permission permission) const;
    void addUser(const std::string& username, const std::string& password, Role role);
    void setEnabled(const std::string& username, bool enabled);
    void changePassword(const std::string& oldPassword, const std::string& newPassword);
    std::vector<Account> listUsers() const;

private:
    FileManager& files_;
    std::optional<std::string> currentUsername_;
};

class ProductManager {
public:
    ProductManager(FileManager& files, User& user);
    std::string addProduct(const ProductDraft& draft);
    void updateProduct(const std::string& id, const ProductDraft& draft);
    void deleteProduct(const std::string& id);
    Product findProduct(const std::string& idOrBarcode) const;
    std::vector<Product> listProducts(const std::string& keyword = "", bool includeInactive = false) const;

private:
    FileManager& files_;
    User& user_;
};

class PurchaseManager {
public:
    PurchaseManager(FileManager& files, User& user, ProductManager& products);
    PurchaseRecord confirmInbound(const std::string& productId, Quantity quantity, Money unitPrice);
    std::vector<PurchaseRecord> queryPurchase(const std::string& startDate = "", const std::string& endDate = "") const;

private:
    FileManager& files_;
    User& user_;
    ProductManager& products_;
};

class SaleManager {
public:
    SaleManager(FileManager& files, User& user, ProductManager& products);
    void addToCart(std::vector<CartItem>& cart, const std::string& productId, Quantity quantity) const;
    void removeFromCart(std::vector<CartItem>& cart, const std::string& productId) const;
    Money quote(const std::vector<CartItem>& cart) const;
    SaleRecord checkout(std::vector<CartItem>& cart, Money paid);
    std::vector<SaleRecord> querySale(const std::string& startDate = "", const std::string& endDate = "") const;

private:
    FileManager& files_;
    User& user_;
    ProductManager& products_;
};

class InventoryManager {
public:
    InventoryManager(FileManager& files, User& user, ProductManager& products);
    StocktakeRecord countInventory(const std::string& productId, Quantity counted, const std::string& reason);
    std::vector<Product> checkLowStock() const;
    std::vector<StocktakeRecord> records() const;

private:
    FileManager& files_;
    User& user_;
    ProductManager& products_;
};

class ReportManager {
public:
    ReportManager(FileManager& files, User& user);
    SalesSummary salesReport(const std::string& startDate = "", const std::string& endDate = "") const;
    PurchaseSummary purchaseReport(const std::string& startDate = "", const std::string& endDate = "") const;
    InventorySummary inventoryReport() const;

private:
    FileManager& files_;
    User& user_;
};

class Menu {
public:
    Menu(FileManager& files, std::istream& input, std::ostream& output);
    void run();

private:
    std::string readLine(const std::string& prompt);
    std::string readPassword(const std::string& prompt);
    std::int64_t readNumber(const std::string& prompt, std::int64_t minimum, std::int64_t maximum);
    Money readMoney(const std::string& prompt);
    bool confirm(const std::string& prompt);
    void showPage(const std::string& title, const std::string& description = "");
    void showMainMenu();
    void dispatch(int choice);
    void productMenu();
    void purchaseMenu();
    void saleMenu();
    void inventoryMenu();
    void reportMenu();
    void accountMenu();
    void printProducts(const std::vector<Product>& products);
    void printSales(const std::vector<SaleRecord>& records);
    void printCart(const std::vector<CartItem>& cart);

    FileManager& files_;
    std::istream& input_;
    std::ostream& output_;
    ConsoleUi ui_;
    User user_;
    ProductManager products_;
    PurchaseManager purchases_;
    SaleManager sales_;
    InventoryManager inventory_;
    ReportManager reports_;
};

}
