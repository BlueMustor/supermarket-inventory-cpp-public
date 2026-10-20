#include "supermarket/services.hpp"
#include "supermarket/console_ui.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace market;
namespace fs = std::filesystem;
constexpr const char* password = "TestPass123!";
std::string sharedHash;
std::size_t assertions = 0;

class Failure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const std::string& description) {
    ++assertions;
    if (!condition) throw Failure(description);
}

template <class Action>
void rejects(Action&& action, const std::string& description) {
    ++assertions;
    try {
        action();
    } catch (const Error&) {
        return;
    }
    throw Failure(description + " (expected market::Error)");
}

std::string readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Failure("cannot open test file for reading");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
}

std::string describe(const State& state) {
    std::ostringstream out;
    out << state.nextProductId << ' ' << state.nextRecordId << '\n';
    for (const auto& account : state.users) {
        out << std::quoted(account.username) << std::quoted(account.passwordHash)
            << static_cast<int>(account.role) << account.enabled << '\n';
    }
    for (const auto& item : state.products.entries()) {
        out << std::quoted(item.productId) << std::quoted(item.barcode) << std::quoted(item.name)
            << std::quoted(item.category) << ' ' << item.purchasePrice << ' ' << item.salePrice
            << ' ' << item.stockQuantity << ' ' << item.minStock << ' ' << item.active << '\n';
    }
    for (const auto& record : state.purchases) {
        out << std::quoted(record.id) << std::quoted(record.timestamp) << std::quoted(record.operatorName)
            << std::quoted(record.productId) << std::quoted(record.productName) << ' ' << record.quantity
            << ' ' << record.unitPrice << ' ' << record.totalCost << '\n';
    }
    for (const auto& record : state.sales) {
        out << std::quoted(record.id) << std::quoted(record.timestamp) << std::quoted(record.operatorName)
            << ' ' << record.total << ' ' << record.paid << ' ' << record.change << '\n';
        for (const auto& item : record.items) {
            out << std::quoted(item.productId) << std::quoted(item.productName) << ' '
                << item.quantity << ' ' << item.unitPrice << ' ' << item.unitCost << '\n';
        }
    }
    for (const auto& record : state.stocktakes) {
        out << std::quoted(record.id) << std::quoted(record.timestamp) << std::quoted(record.operatorName)
            << std::quoted(record.productId) << std::quoted(record.productName) << ' ' << record.beforeQuantity
            << ' ' << record.countedQuantity << std::quoted(record.reason) << '\n';
    }
    return out.str();
}

ProductDraft draft(std::string barcode = "690001", std::string name = "Milk") {
    return {std::move(barcode), std::move(name), "Food", 100, 250, 2};
}

struct Fixture {
    explicit Fixture(const fs::path& path)
        : files(path), user(files), products(files, user), purchases(files, user, products),
          sales(files, user, products), inventory(files, user, products), reports(files, user) {
        State state;
        state.users = {{"admin", sharedHash, Role::Admin, true},
                       {"cash_a", sharedHash, Role::Cashier, true},
                       {"cash_b", sharedHash, Role::Cashier, true}};
        files.commit(std::move(state));
        user.login("admin", password);
    }

    std::string addStock(Quantity quantity = 5, const ProductDraft& product = draft()) {
        const auto id = products.addProduct(product);
        if (quantity > 0) purchases.confirmInbound(id, quantity, product.purchasePrice);
        return id;
    }

    FileManager files;
    User user;
    ProductManager products;
    PurchaseManager purchases;
    SaleManager sales;
    InventoryManager inventory;
    ReportManager reports;
};

void moneyParsing(const fs::path&) {
    check(parseMoney("0") == 0, "zero money");
    check(parseMoney("12") == 1200, "whole yuan");
    check(parseMoney("12.3") == 1230, "one decimal digit");
    check(parseMoney("00012.34") == 1234, "leading zero money");
    check(parseMoney("10000000000.00") == maxMoney, "maximum money");
    check(formatMoney(1) == "0.01", "money leading fractional zero");
    check(formatMoney(-1234) == "-12.34", "negative profit formatting");
    check(formatMoney(std::numeric_limits<Money>::min()) == "-92233720368547758.08", "minimum integer formatting");
    for (const auto* text : {"", " ", "-1", "+1", ".50", "1.", "1.234", "1e2", "1,23", "12x", "1.2.3", "10000000000.01"}) {
        rejects([&] { parseMoney(text); }, "invalid money rejected");
    }
    check(checkedAdd(maxMoney - 1, 1) == maxMoney, "addition boundary");
    check(checkedMultiply(125, 4) == 500, "exact integer multiplication");
    rejects([] { checkedAdd(maxMoney, 1); }, "addition overflow");
    rejects([] { checkedAdd(-1, 1); }, "negative addition input");
    rejects([] { checkedMultiply(maxMoney, 2); }, "multiplication overflow");
    rejects([] { checkedMultiply(1, maxQuantity + 1); }, "quantity overflow");
}

void integerParsing(const fs::path&) {
    check(parseInteger("0", 0, 10) == 0, "integer lower bound");
    check(parseInteger("10", 0, 10) == 10, "integer upper bound");
    check(parseInteger("-3", -5, 5) == -3, "signed integer");
    for (const auto* text : {"", " ", "+1", "1x", "1.0", " 1", "1 ", "999999999999999999999999"}) {
        rejects([&] { parseInteger(text, 0, 10); }, "invalid integer rejected");
    }
    rejects([] { parseInteger("11", 0, 10); }, "integer out of bounds");
    rejects([] { parseInteger("1", 2, 1); }, "inverted integer bounds");
}

void calendarValidation(const fs::path&) {
    validateDate("2000-02-29");
    validateDate("2024-02-29");
    validateDate("2026-10-18");
    validateDateRange("", "");
    check(inDateRange("2026-10-18 23:59:59", "2026-10-18", "2026-10-18"), "inclusive dates");
    check(!inDateRange("2026-10-19 00:00:00", "2026-10-18", "2026-10-18"), "end date cutoff");
    for (const auto* text : {"1900-02-29", "2026-02-29", "2026-04-31", "2026-00-01", "2026-13-01", "2026-10-00", "2026-1-01", "2026/10/18", "20a6-10-18", "1899-12-31"}) {
        rejects([&] { validateDate(text); }, "invalid calendar date");
    }
    rejects([] { validateDateRange("2026-10-20", "2026-10-19"); }, "reversed dates");
    rejects([] { validateDateRange("", "2026-02-30"); }, "open range still validates date");
}

void utf8Validation(const fs::path&) {
    requireText("中文商品 🍎", "test");
    requireText("A \"quoted\" \\ item", "test");
    check(true, "valid UTF-8 and escaped punctuation");
    for (const auto& text : std::vector<std::string>{"", "   ", "a\nb", "a\tb", std::string(1, '\0'),
             std::string("\xC0\xAF", 2), std::string("\xED\xA0\x80", 3), std::string("\xF4\x90\x80\x80", 4),
             std::string("\xE4\xB8", 2), std::string("\x80", 1)}) {
        rejects([&] { requireText(text, "test"); }, "invalid text or UTF-8");
    }
    rejects([] { requireText("中文", "test", 5); }, "text limit counts bytes");
}

std::uint64_t testHash(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char ch : text) { hash ^= ch; hash *= 1099511628211ULL; }
    return hash;
}

Product tableProduct(std::string id) {
    Product product;
    product.productId = std::move(id);
    product.name = product.productId;
    return product;
}

void hashCollisions(const fs::path&) {
    ProductTable table;
    std::map<std::size_t, std::vector<std::string>> groups;
    std::vector<std::string> collision;
    for (int index = 1; collision.size() < 3; ++index) {
        const auto id = "collision_" + std::to_string(index);
        auto& group = groups[static_cast<std::size_t>(testHash(id)) % table.bucketCount()];
        group.push_back(id);
        if (group.size() == 3) collision = group;
    }
    for (const auto& id : collision) table.insert(tableProduct(id));
    check(table.size() == 3, "collisions retain all elements");
    for (const auto& id : collision) check(table.find(id) != nullptr && table.find(id)->name == id, "collision lookup");
    rejects([&] { table.insert(tableProduct(collision.front())); }, "duplicate hash key");
    check(table.size() == 3, "duplicate does not change size");
    check(table.find("missing") == nullptr, "absent hash key");
}

void hashGrowthAndCopy(const fs::path&) {
    ProductTable table;
    const auto initialBuckets = table.bucketCount();
    for (int index = 0; index < 300; ++index) table.insert(tableProduct("id_" + std::to_string(index)));
    check(table.bucketCount() > initialBuckets, "hash expands");
    check(table.size() == 300, "rehash retains count");
    const ProductTable& constant = table;
    for (int index = 0; index < 300; ++index) check(constant.find("id_" + std::to_string(index)) != nullptr, "rehash retains keys");
    ProductTable copy = table;
    copy.find("id_0")->name = "Changed";
    copy.insert(tableProduct("extra"));
    check(table.find("id_0")->name == "id_0", "copy owns its elements");
    check(table.find("extra") == nullptr && copy.size() == 301, "copy insertion is independent");
    const auto entries = table.entries();
    check(std::is_sorted(entries.begin(), entries.end(), [](const Product& a, const Product& b) { return a.productId < b.productId; }), "entries have stable key order");
}

void passwordStorage(const fs::path&) {
    check(validPasswordHash(sharedHash), "supported password hash");
    check(sharedHash.find(password) == std::string::npos, "hash excludes plaintext");
    check(verifyPassword(password, sharedHash), "correct password verifies");
    check(!verifyPassword("WrongPass123!", sharedHash), "wrong password fails");
    check(!verifyPassword(password, "pbkdf2-sha256$1$invalid"), "malformed hash fails safely");
    const auto other = hashPassword(password);
    check(other != sharedHash, "independent random salt");
    check(verifyPassword(password, other), "salted hash still verifies");
    rejects([] { hashPassword("short"); }, "short password rejected");
    rejects([] { hashPassword(std::string(129, 'a')); }, "long password rejected");
}

void firstAdministrator(const fs::path& path) {
    FileManager files(path);
    User user(files);
    check(!user.hasUsers() && !user.isLoggedIn(), "new store has no default account");
    rejects([&] { user.bootstrapAdmin("a", password); }, "short username rejected");
    rejects([&] { user.bootstrapAdmin("bad name", password); }, "invalid username rejected");
    rejects([&] { user.bootstrapAdmin("admin", "short"); }, "invalid bootstrap password");
    check(files.data().users.empty(), "failed bootstrap is atomic");
    user.bootstrapAdmin("admin", password);
    check(user.hasUsers() && !user.isLoggedIn(), "bootstrap does not auto login");
    check(files.data().users.front().role == Role::Admin, "first user is administrator");
    rejects([&] { user.bootstrapAdmin("another", password); }, "bootstrap allowed only once");
    user.login("admin", password);
    check(user.current().username == "admin", "bootstrap account can login");
}

void accountLifecycle(const fs::path& path) {
    Fixture fixture(path);
    auto& user = fixture.user;
    user.addUser("new_cashier", password, Role::Cashier);
    check(fixture.files.data().users.size() == 4, "account created");
    rejects([&] { user.addUser("new_cashier", password, Role::Cashier); }, "duplicate username rejected");
    rejects([&] { user.addUser("x", password, Role::Cashier); }, "short username rejected");
    rejects([&] { user.addUser("bad-name", password, Role::Cashier); }, "restricted username alphabet");
    rejects([&] { user.addUser("valid_name", password, static_cast<Role>(99)); }, "unknown role rejected");
    for (const auto& account : user.listUsers()) check(account.passwordHash.empty(), "account list redacts password hash");
    rejects([&] { user.setEnabled("admin", false); }, "cannot disable current administrator");
    rejects([&] { user.setEnabled("unknown", false); }, "missing account rejected");
    User session(fixture.files);
    session.login("new_cashier", password);
    user.setEnabled("new_cashier", false);
    check(!session.isLoggedIn(), "disabled account invalidates active session");
    rejects([&] { session.current(); }, "disabled session cannot access identity");
    rejects([&] { session.login("new_cashier", password); }, "disabled login rejected");
    rejects([&] { user.addUser("new_cashier", password, Role::Cashier); }, "disabled username cannot be reused");
    user.setEnabled("new_cashier", true);
    session.login("new_cashier", password);
    check(session.isLoggedIn(), "enabled account can login again");
    rejects([&] { session.login("new_cashier", "WrongPass123!"); }, "failed login rejected");
    check(!session.isLoggedIn(), "failed login clears previous session");
}

void passwordChanges(const fs::path& path) {
    Fixture fixture(path);
    const auto original = describe(fixture.files.data());
    rejects([&] { fixture.user.changePassword("WrongPass123!", "NewPass123!"); }, "old password required");
    rejects([&] { fixture.user.changePassword(password, password); }, "same password rejected");
    rejects([&] { fixture.user.changePassword(password, "short"); }, "new password length enforced");
    check(describe(fixture.files.data()) == original, "failed changes preserve account");
    fixture.user.changePassword(password, "NewPass123!");
    check(fixture.user.isLoggedIn(), "password change retains current session");
    fixture.user.logout();
    rejects([&] { fixture.user.login("admin", password); }, "old password no longer works");
    fixture.user.login("admin", "NewPass123!");
    check(fixture.user.current().username == "admin", "new password works");
}

void cashierPermissions(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock();
    f.user.login("cash_a", password);
    const auto before = describe(f.files.data());
    rejects([&] { f.user.addUser("forbidden", password, Role::Admin); }, "cashier cannot add users");
    rejects([&] { f.user.setEnabled("cash_b", false); }, "cashier cannot disable users");
    rejects([&] { f.user.listUsers(); }, "cashier cannot list users");
    rejects([&] { f.products.addProduct(draft("other")); }, "cashier cannot add products");
    rejects([&] { f.products.updateProduct(id, draft()); }, "cashier cannot update products");
    rejects([&] { f.products.deleteProduct(id); }, "cashier cannot disable products");
    rejects([&] { f.products.listProducts("", true); }, "cashier cannot see inactive products");
    rejects([&] { f.purchases.confirmInbound(id, 1, 100); }, "cashier cannot purchase");
    rejects([&] { f.purchases.queryPurchase(); }, "cashier cannot view procurement");
    rejects([&] { f.inventory.countInventory(id, 1, "forbidden"); }, "cashier cannot count inventory");
    rejects([&] { f.inventory.checkLowStock(); }, "cashier cannot view low stock report");
    rejects([&] { f.inventory.records(); }, "cashier cannot view stocktakes");
    rejects([&] { f.reports.salesReport(); }, "cashier cannot view sales report");
    rejects([&] { f.reports.purchaseReport(); }, "cashier cannot view purchase report");
    rejects([&] { f.reports.inventoryReport(); }, "cashier cannot view inventory report");
    rejects([&] { f.files.backupData(f.user); }, "cashier cannot backup");
    check(describe(f.files.data()) == before, "denied calls do not mutate state");
    check(f.products.listProducts().size() == 1, "cashier can list active products");
    check(f.products.findProduct("690001").productId == id, "cashier can look up barcode");
    std::vector<CartItem> cart;
    f.sales.addToCart(cart, id, 1);
    check(f.sales.quote(cart) == 250, "cashier can quote");
    const auto sale = f.sales.checkout(cart, 300);
    check(sale.operatorName == "cash_a" && sale.change == 50, "cashier can checkout");
}

void loggedOutServices(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock();
    f.user.logout();
    check(!f.user.isLoggedIn(), "logout clears session");
    std::vector<CartItem> cart{{id, 1}};
    rejects([&] { f.user.current(); }, "identity requires login");
    rejects([&] { f.user.changePassword(password, "NewPass123!"); }, "password change requires login");
    rejects([&] { f.products.findProduct(id); }, "product lookup requires login");
    rejects([&] { f.products.listProducts(); }, "product list requires login");
    rejects([&] { f.purchases.queryPurchase(); }, "purchase history requires login");
    rejects([&] { f.sales.addToCart(cart, id, 1); }, "cart add requires login");
    rejects([&] { f.sales.removeFromCart(cart, id); }, "cart removal requires login");
    rejects([&] { f.sales.quote(cart); }, "quote requires login");
    rejects([&] { f.sales.checkout(cart, 250); }, "checkout requires login");
    rejects([&] { f.sales.querySale(); }, "sales history requires login");
    rejects([&] { f.inventory.checkLowStock(); }, "inventory requires login");
    rejects([&] { f.reports.salesReport(); }, "reports require login");
    rejects([&] { f.files.backupData(f.user); }, "backup requires login");
    check(cart.size() == 1 && f.files.data().sales.empty(), "denied sale preserves cart");
}

void productValidation(const fs::path& path) {
    Fixture f(path);
    const auto id = f.products.addProduct(draft());
    check(id == "P000001" && f.products.findProduct(id).stockQuantity == 0, "product starts with zero stock");
    check(f.products.findProduct("690001").productId == id, "barcode resolves canonical id");
    check(f.products.listProducts("Milk").size() == 1, "name keyword search");
    check(f.products.listProducts("Food").size() == 1, "category keyword search");
    const auto before = describe(f.files.data());
    rejects([&] { f.products.addProduct(draft()); }, "duplicate barcode");
    rejects([&] { f.products.addProduct(draft("P000099")); }, "reserved barcode namespace");
    auto invalid = draft("another");
    invalid.name.clear();
    rejects([&] { f.products.addProduct(invalid); }, "missing product name");
    invalid = draft("another"); invalid.salePrice = -1;
    rejects([&] { f.products.addProduct(invalid); }, "negative price");
    invalid = draft("another"); invalid.minStock = maxQuantity + 1;
    rejects([&] { f.products.addProduct(invalid); }, "invalid stock threshold");
    check(describe(f.files.data()) == before, "invalid products preserve data and id counter");
    auto updated = draft("new_barcode", "New Milk");
    f.products.updateProduct(id, updated);
    check(f.products.findProduct("new_barcode").name == "New Milk", "product update replaces fields");
    rejects([&] { f.products.findProduct("690001"); }, "old barcode no longer resolves");
}

void softDeletion(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock(2);
    rejects([&] { f.products.deleteProduct(id); }, "cannot hide product with stock");
    f.inventory.countInventory(id, 0, "Return remaining stock");
    f.products.deleteProduct(id);
    check(!f.products.findProduct(id).active, "zero-stock product becomes inactive");
    check(f.products.listProducts().empty() && f.products.listProducts("", true).size() == 1, "inactive filtering");
    check(f.files.data().purchases.size() == 1 && f.files.data().stocktakes.size() == 1, "deletion retains audit trail");
    rejects([&] { f.products.addProduct(draft()); }, "inactive barcode stays reserved");
    rejects([&] { f.products.updateProduct(id, draft()); }, "inactive product cannot change");
    rejects([&] { f.purchases.confirmInbound(id, 1, 100); }, "inactive product cannot be purchased");
    rejects([&] { f.inventory.countInventory(id, 1, "invalid"); }, "inactive product cannot be counted");
    std::vector<CartItem> cart;
    rejects([&] { f.sales.addToCart(cart, id, 1); }, "inactive product cannot be sold");
    f.user.login("cash_a", password);
    rejects([&] { f.products.findProduct(id); }, "cashier cannot access inactive product");
}

void procurementBoundaries(const fs::path& path) {
    Fixture f(path);
    const auto id = f.products.addProduct(draft());
    const auto before = describe(f.files.data());
    rejects([&] { f.purchases.confirmInbound(id, 0, 100); }, "zero procurement quantity");
    rejects([&] { f.purchases.confirmInbound(id, -1, 100); }, "negative procurement quantity");
    rejects([&] { f.purchases.confirmInbound(id, maxQuantity + 1, 100); }, "procurement quantity maximum");
    rejects([&] { f.purchases.confirmInbound(id, 1, -1); }, "negative procurement cost");
    rejects([&] { f.purchases.confirmInbound(id, 2, maxMoney); }, "procurement amount overflow");
    check(describe(f.files.data()) == before, "invalid procurement is atomic");
    const auto record = f.purchases.confirmInbound(id, maxQuantity, 0);
    check(record.quantity == maxQuantity && record.totalCost == 0, "quantity boundary and free procurement");
    check(f.products.findProduct(id).stockQuantity == maxQuantity, "maximum stock persisted");
    const auto full = describe(f.files.data());
    rejects([&] { f.purchases.confirmInbound(id, 1, 0); }, "stock cannot exceed maximum");
    check(describe(f.files.data()) == full, "stock overflow leaves history unchanged");
}

void cartMergeAndRemoval(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock(5);
    const auto emptyId = f.products.addProduct(draft("empty", "Empty"));
    std::vector<CartItem> cart;
    f.sales.addToCart(cart, id, 2);
    f.sales.addToCart(cart, "690001", 1);
    check(cart.size() == 1 && cart.front().productId == id && cart.front().quantity == 3, "same product merges by canonical id");
    check(f.sales.quote(cart) == 750, "merged quote");
    rejects([&] { f.sales.addToCart(cart, id, 3); }, "merged quantity cannot exceed stock");
    rejects([&] { f.sales.addToCart(cart, emptyId, 1); }, "zero-stock product cannot enter cart");
    rejects([&] { f.sales.addToCart(cart, id, 0); }, "zero cart quantity rejected");
    rejects([&] { f.sales.addToCart(cart, id, maxQuantity); }, "huge merged cart rejected");
    check(cart.size() == 1 && cart.front().quantity == 3, "failed cart addition preserves cart");
    rejects([&] { f.sales.removeFromCart(cart, emptyId); }, "remove absent cart product");
    f.sales.removeFromCart(cart, "690001");
    check(cart.empty(), "barcode removal removes whole cart line");
    check(f.products.findProduct(id).stockQuantity == 5 && f.files.data().sales.empty(), "cart edits do not alter stock");
}

void malformedCarts(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock();
    rejects([&] { f.sales.quote({}); }, "empty cart quote rejected");
    rejects([&] { f.sales.quote({{id, 1}, {id, 1}}); }, "duplicate cart lines rejected");
    rejects([&] { f.sales.quote({{"690001", 1}}); }, "cart stores canonical ids only");
    rejects([&] { f.sales.quote({{id, -1}}); }, "negative line quantity rejected");
    rejects([&] { f.sales.quote({{id, 6}}); }, "insufficient stock rejected");
    rejects([&] { f.sales.quote({{"missing", 1}}); }, "missing cart product rejected");
    check(f.files.data().sales.empty() && f.products.findProduct(id).stockQuantity == 5, "malformed quotes are read-only");
}

void checkoutAtomicValidation(const fs::path& path) {
    Fixture f(path);
    const auto first = f.addStock(5);
    const auto second = f.addStock(1, draft("690002", "Bread"));
    std::vector<CartItem> cart{{first, 2}, {second, 2}};
    const auto state = describe(f.files.data());
    const auto disk = readBytes(f.files.directory() / "store.db");
    rejects([&] { f.sales.checkout(cart, 1000); }, "later line lacks stock");
    check(describe(f.files.data()) == state, "multi-product failure changes no inventory or counter");
    check(readBytes(f.files.directory() / "store.db") == disk, "multi-product failure changes no disk bytes");
    check(cart.size() == 2 && cart[0].quantity == 2 && cart[1].quantity == 2, "multi-product failure retains cart");
    cart[1].quantity = 1;
    rejects([&] { f.sales.checkout(cart, 749); }, "underpayment rejected");
    rejects([&] { f.sales.checkout(cart, maxMoney + 1); }, "over-limit payment rejected");
    check(describe(f.files.data()) == state && cart.size() == 2, "payment failure preserves transaction");
    const auto sale = f.sales.checkout(cart, 1000);
    check(sale.total == 750 && sale.change == 250 && sale.items.size() == 2, "multi-product checkout totals");
    check(cart.empty() && f.files.data().sales.size() == 1, "successful checkout records once and clears cart");
    check(f.products.findProduct(first).stockQuantity == 3 && f.products.findProduct(second).stockQuantity == 0, "successful checkout decrements every line");
}

void checkoutCostOverflow(const fs::path& path) {
    Fixture f(path);
    auto expensive = draft("cost_a", "Cost A");
    expensive.purchasePrice = 600'000'000'000LL;
    expensive.salePrice = 0;
    const auto first = f.addStock(1, expensive);
    expensive.barcode = "cost_b"; expensive.name = "Cost B";
    const auto second = f.addStock(1, expensive);
    std::vector<CartItem> cart{{first, 1}, {second, 1}};
    check(f.sales.quote(cart) == 0, "zero-price products can be quoted");
    const auto before = describe(f.files.data());
    const auto disk = readBytes(f.files.directory() / "store.db");
    rejects([&] { f.sales.checkout(cart, 0); }, "whole-receipt cost cannot exceed monetary limit");
    check(describe(f.files.data()) == before && readBytes(f.files.directory() / "store.db") == disk, "cost overflow preserves stock records and counter");
    check(cart.size() == 2 && cart[0].quantity == 1 && cart[1].quantity == 1, "cost overflow preserves cart");
    cart = {{first, 1}};
    f.sales.checkout(cart, 0);
    cart = {{second, 1}};
    f.sales.checkout(cart, 0);
    check(f.files.data().sales.size() == 2, "separate supported-cost receipts can succeed");
    rejects([&] { f.reports.salesReport(); }, "cross-receipt aggregate cost overflow is explicit");
    rejects([&] { f.reports.purchaseReport(); }, "aggregate procurement overflow is explicit");
}

void historicalSnapshots(const fs::path& path) {
    Fixture f(path);
    const std::string originalName = "旧商品 \"A\" \\ 苹果";
    const auto id = f.addStock(4, draft("690001", originalName));
    std::vector<CartItem> cart{{id, 2}};
    const auto sale = f.sales.checkout(cart, 1000);
    check(sale.total == 500 && sale.paid == 1000 && sale.change == 500, "sale receipt amounts");
    check(sale.items.front().productName == originalName && sale.items.front().unitPrice == 250 && sale.items.front().unitCost == 100, "sale captures name price and cost");
    auto replacement = draft("690001", "新商品");
    replacement.purchasePrice = 120; replacement.salePrice = 300;
    f.products.updateProduct(id, replacement);
    f.purchases.confirmInbound(id, 1, 150);
    const auto& stored = f.files.data().sales.front().items.front();
    check(stored.productName == originalName && stored.unitPrice == 250 && stored.unitCost == 100, "later edits do not rewrite past sale");
    check(f.files.data().purchases.front().productName == originalName && f.files.data().purchases.front().unitPrice == 100, "old procurement snapshot preserved");
    check(f.files.data().purchases.back().productName == "新商品", "new procurement uses new name");
    const auto report = f.reports.salesReport();
    check(report.revenue == 500 && report.cost == 200 && report.grossProfit == 300, "profit uses historical cost");
    const auto inventory = f.reports.inventoryReport();
    check(inventory.totalQuantity == 3 && inventory.costValue == 450 && inventory.retailValue == 900, "inventory estimate uses current reference prices");
}

void stocktakeAudit(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock(3);
    const auto before = describe(f.files.data());
    rejects([&] { f.inventory.countInventory(id, -1, "invalid"); }, "negative stocktake rejected");
    rejects([&] { f.inventory.countInventory(id, maxQuantity + 1, "invalid"); }, "over-limit stocktake rejected");
    rejects([&] { f.inventory.countInventory(id, 2, " "); }, "stocktake reason required");
    check(describe(f.files.data()) == before, "invalid stocktake is atomic");
    const auto record = f.inventory.countInventory(id, 2, "核实损耗 1 件");
    check(record.beforeQuantity == 3 && record.countedQuantity == 2 && record.operatorName == "admin", "stocktake before after and operator");
    check(record.reason == "核实损耗 1 件" && record.productId == id, "stocktake reason and product");
    check(f.inventory.records().size() == 1 && f.products.findProduct(id).stockQuantity == 2, "stocktake changes inventory with record");
    check(f.inventory.checkLowStock().size() == 1, "warning triggers at threshold");
    f.inventory.countInventory(id, 5, "盘点补录");
    check(f.inventory.records().back().beforeQuantity == 2 && f.inventory.checkLowStock().empty(), "successive stocktakes use current stock");
}

void datedReports(const fs::path& path) {
    Fixture f(path);
    const auto first = f.addStock(10);
    auto other = draft("690002", "Bread"); other.purchasePrice = 200; other.salePrice = 400;
    const auto second = f.addStock(5, other);
    std::vector<CartItem> cart{{first, 2}, {second, 1}};
    f.sales.checkout(cart, 1000);
    f.purchases.confirmInbound(first, 2, 120);
    cart = {{first, 1}};
    f.sales.checkout(cart, 250);
    State dated = f.files.data();
    dated.purchases[0].timestamp = "2026-10-09 08:00:00";
    dated.purchases[1].timestamp = "2026-10-10 09:00:00";
    dated.purchases[2].timestamp = "2026-10-11 10:00:00";
    dated.sales[0].timestamp = "2026-10-10 23:59:59";
    dated.sales[1].timestamp = "2026-10-11 00:00:00";
    f.files.commit(std::move(dated));
    check(f.sales.querySale("2026-10-10", "2026-10-10").size() == 1, "sales single-day query");
    check(f.purchases.queryPurchase("2026-10-10", "2026-10-11").size() == 2, "purchase inclusive range");
    check(f.sales.querySale("", "2026-10-10").size() == 1, "open-start date range");
    check(f.sales.querySale("2026-10-11", "").size() == 1, "open-end date range");
    const auto day = f.reports.salesReport("2026-10-10", "2026-10-10");
    check(day.transactionCount == 1 && day.quantity == 3 && day.revenue == 900 && day.cost == 400 && day.grossProfit == 500, "daily sales metrics");
    const auto all = f.reports.salesReport();
    check(all.transactionCount == 2 && all.quantity == 4 && all.revenue == 1150 && all.cost == 520 && all.grossProfit == 630, "all sales metrics");
    const auto purchase = f.reports.purchaseReport();
    check(purchase.transactionCount == 3 && purchase.quantity == 17 && purchase.totalCost == 2240, "procurement metrics");
    const auto inventory = f.reports.inventoryReport();
    check(inventory.activeProducts == 2 && inventory.totalQuantity == 13 && inventory.costValue == 1880 && inventory.retailValue == 3850 && inventory.lowStockCount == 0, "current inventory metrics");
    check(f.reports.salesReport("2020-01-01", "2020-12-31").transactionCount == 0, "empty report period");
    rejects([&] { f.sales.querySale("2026-10-12", "2026-10-01"); }, "sales reject reversed dates");
    rejects([&] { f.purchases.queryPurchase("2026-02-30", ""); }, "procurement rejects invalid date");
    rejects([&] { f.reports.purchaseReport("2026-10-12", "2026-10-01"); }, "reports reject reversed dates");
}

void ownSalesVisibility(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock(10);
    std::vector<CartItem> cart{{id, 1}};
    f.sales.checkout(cart, 250);
    f.user.login("cash_a", password);
    cart = {{id, 1}}; const auto own = f.sales.checkout(cart, 250);
    check(f.sales.querySale().size() == 1 && f.sales.querySale().front().id == own.id, "cashier sees own sale only");
    f.user.login("cash_b", password);
    check(f.sales.querySale().empty(), "other cashier cannot see colleagues sales");
    cart = {{id, 1}}; f.sales.checkout(cart, 250);
    check(f.sales.querySale().size() == 1 && f.sales.querySale().front().operatorName == "cash_b", "second cashier sees own sale");
    f.user.login("admin", password);
    check(f.sales.querySale().size() == 3, "administrator sees all sales");
}

void invalidCommitAtomicity(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock(3);
    const auto before = describe(f.files.data());
    const auto disk = readBytes(f.files.directory() / "store.db");
    State invalid = f.files.data(); invalid.products.find(id)->stockQuantity = 99;
    rejects([&] { f.files.commit(invalid); }, "stock must agree with event ledger");
    invalid = f.files.data(); invalid.users.front().enabled = false;
    rejects([&] { f.files.commit(invalid); }, "store needs an enabled administrator");
    invalid = f.files.data(); invalid.nextRecordId = 1;
    rejects([&] { f.files.commit(invalid); }, "record counter cannot reuse existing id");
    invalid = f.files.data(); invalid.purchases.front().operatorName = "missing";
    rejects([&] { f.files.commit(invalid); }, "record operator must exist");
    check(describe(f.files.data()) == before, "invalid snapshots never change memory");
    check(readBytes(f.files.directory() / "store.db") == disk, "invalid snapshots never change disk");
}

void persistenceRoundTrip(const fs::path& path) {
    std::string expected;
    std::string id;
    {
        Fixture f(path);
        id = f.addStock(7, draft("中文条码", "牛奶 \"原味\" \\ 🍎"));
        std::vector<CartItem> cart{{id, 2}}; f.sales.checkout(cart, 600);
        f.inventory.countInventory(id, 4, "复核 \"货架\" \\ 库房");
        f.user.setEnabled("cash_b", false);
        expected = describe(f.files.data());
    }
    FileManager reopened(path);
    check(describe(reopened.data()) == expected, "all fields survive disk reopen");
    User user(reopened); user.login("admin", password);
    ProductManager products(reopened, user);
    check(products.findProduct("中文条码").productId == id && products.findProduct(id).stockQuantity == 4, "UTF-8 lookup after reopen");
    check(reopened.data().sales.front().items.front().productName == "牛奶 \"原味\" \\ 🍎", "quoted UTF-8 history survives");
    rejects([&] { user.login("cash_b", password); }, "disabled state survives reopen");
}

void corruptionTest(const fs::path& path, const std::function<std::string(std::string)>& damage) {
    {
        Fixture f(path); f.addStock(3);
    }
    const auto store = path / "store.db";
    const auto damaged = damage(readBytes(store));
    writeBytes(store, damaged);
    rejects([&] { FileManager rejected(path); }, "corrupt store rejected on open");
    check(readBytes(store) == damaged, "corrupt store never overwritten");
    rejects([&] { FileManager rejectedAgain(path); }, "failed load releases directory lock");
    check(readBytes(store) == damaged, "repeated failed open preserves evidence");
}

void corruptHeader(const fs::path& path) {
    corruptionTest(path, [](std::string bytes) {
        const auto marker = bytes.find("MARKET_STORE 1");
        if (marker == std::string::npos) throw Failure("fixture lacks storage header");
        bytes.replace(marker, 14, "MARKET_STORE 99");
        return bytes;
    });
}

void corruptInventory(const fs::path& path) {
    corruptionTest(path, [](std::string bytes) {
        const auto marker = bytes.find(" 100 250 3 2 1\n");
        if (marker == std::string::npos) throw Failure("fixture lacks expected product fields");
        bytes.replace(marker, 15, " 100 250 99 2 1\n");
        return bytes;
    });
}

void corruptTrailingData(const fs::path& path) {
    corruptionTest(path, [](std::string bytes) { return bytes + "UNEXPECTED DATA\n"; });
}

void directoryLocking(const fs::path& path) {
    std::string expected;
    {
        Fixture f(path); f.addStock();
        expected = readBytes(path / "store.db");
        rejects([&] { FileManager other(path); }, "second instance cannot acquire lock");
        rejects([&] { FileManager alias(path / "."); }, "canonical alias cannot bypass lock");
        check(readBytes(path / "store.db") == expected, "failed second open preserves file");
    }
    FileManager reopened(path);
    check(reopened.data().products.size() == 1, "closing owner releases lock");
}

void backupsRestore(const fs::path& path) {
    Fixture f(path / "source");
    const auto id = f.addStock(5);
    const auto expected = describe(f.files.data());
    const auto first = f.files.backupData(f.user);
    const auto second = f.files.backupData(f.user);
    check(first != second && fs::is_regular_file(first) && fs::is_regular_file(second), "rapid backups have unique names");
    check(readBytes(first) == readBytes(second), "unchanged backups have identical content");
    f.inventory.countInventory(id, 3, "After backup");
    check(readBytes(first) != readBytes(f.files.directory() / "store.db"), "later writes preserve old backup");
    const auto restoredPath = path / "restored";
    fs::create_directory(restoredPath);
    fs::copy_file(first, restoredPath / "store.db");
    FileManager restored(restoredPath);
    check(describe(restored.data()) == expected, "backup is a complete reopenable snapshot");
    check(restored.data().products.find(id)->stockQuantity == 5, "restored inventory reflects backup moment");
}

void failedSavePreservesCheckout(const fs::path& path) {
    Fixture f(path);
    const auto id = f.addStock(5);
    std::vector<CartItem> cart{{id, 2}};
    const auto original = describe(f.files.data());
    const auto counter = f.files.data().nextRecordId;
    const auto store = f.files.directory() / "store.db";
    const auto originalBytes = readBytes(store);
    {
#ifdef _WIN32
        struct HeldFile {
            explicit HeldFile(const fs::path& filename)
                : handle(CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
                if (handle == INVALID_HANDLE_VALUE) throw Failure("cannot hold test store against replacement");
            }
            ~HeldFile() { CloseHandle(handle); }
            HANDLE handle;
        } held(store);
#else
        struct HeldFile {
            explicit HeldFile(fs::path filename) : path(std::move(filename)), saved(path.string() + ".held") {
                fs::rename(path, saved); fs::create_directory(path);
            }
            ~HeldFile() { fs::remove(path); fs::rename(saved, path); }
            fs::path path;
            fs::path saved;
        } held(store);
#endif
        rejects([&] { f.sales.checkout(cart, 1000); }, "disk replacement failure reaches caller");
        check(describe(f.files.data()) == original, "failed save preserves all memory state");
        check(cart.size() == 1 && cart.front().quantity == 2, "failed save preserves cart");
        check(f.files.data().nextRecordId == counter, "failed save preserves next id");
#ifdef _WIN32
        check(readBytes(store) == originalBytes, "failed replacement preserves original disk bytes");
#else
        check(readBytes(held.saved) == originalBytes, "failed replacement preserves original disk bytes");
#endif
    }
    std::uint64_t expectedCounter = counter;
    const auto expectedId = makeId('S', expectedCounter);
    const auto sale = f.sales.checkout(cart, 1000);
    check(sale.id == expectedId && f.files.data().nextRecordId == expectedCounter, "retry consumes exactly one id");
    check(f.files.data().sales.size() == 1 && f.products.findProduct(id).stockQuantity == 3, "retry records and deducts exactly once");
    check(cart.empty(), "successful retry clears cart");
    for (const auto& entry : fs::directory_iterator(f.files.directory())) {
        check(entry.path().filename().u8string().find("store.db.tmp_") != 0, "failed save leaves no temporary snapshots");
    }
}

void consoleTextWidths(const fs::path&) {
    check(ConsoleUi::displayWidth("A中文B") == 6, "mixed Chinese and Latin terminal width");
    check(ConsoleUi::displayWidth("e\u0301中") == 3, "combining accent occupies no additional cell");
    check(ConsoleUi::fit("e\u0301", 1) == "e\u0301", "fit retains combining accent with its base");
    check(ConsoleUi::fit("12.50", 10, true) == "     12.50", "amounts align at right edge");
    check(ConsoleUi::fit("12.50", 10) == "12.50     ", "text aligns at left edge");
    const std::string name = "Wholegrain中文燕麦面包家庭装🍎";
    for (std::size_t width = 0; width <= 24; ++width) {
        const auto fitted = ConsoleUi::fit(name, width);
        requireText(fitted + "A", "fitted text", 2048);
        check(ConsoleUi::displayWidth(fitted) == width, "UTF-8 truncation keeps the requested display width");
    }
}

void consolePlainRendering(const fs::path&) {
    std::istringstream input("untouched\n");
    std::ostringstream output;
    ConsoleUi ui(input, output);
    check(!ui.interactive(), "stream-based UI does not use interactive terminal behavior");
    ui.table({{"商品", 24}, {"售价", 10, true}, {"库存", 8, true}},
             {{"Milk", "2.50", "8"}, {"原味牛奶", "12.00", "120"},
              {"Wholegrain 燕麦面包家庭分享加量装", "10000000000.00", "1000000000"}});
    const auto table = output.str();
    check(table.find("Milk") != std::string::npos && table.find("原味牛奶") != std::string::npos,
          "table renders different-length Chinese and Latin names");
    check(table.find("10000000000.00") != std::string::npos && table.find("1000000000") != std::string::npos,
          "table preserves complete maximum amount and quantity values");
    std::istringstream tableLines(table);
    std::string line;
    std::size_t expectedWidth = 0;
    while (std::getline(tableLines, line)) {
        if (line.empty()) continue;
        const auto width = ConsoleUi::displayWidth(line);
        if (expectedWidth == 0) expectedWidth = width;
        check(width == expectedWidth && width <= ui.width(), "all table borders and rows align within terminal width");
    }
    ui.page("经营工作台", "管理员 admin");
    ui.menu({{"1", "商品中心", "查询与维护商品"}, {"0", "退出系统", "结束本次操作"}});
    ui.metrics({{"今日销售", "12.50"}, {"库存数量", "120"}});
    ui.table({{"商品", 12}}, {}, "没有符合条件的商品");
    ui.prompt("请选择：");
    ui.pause();
    const auto all = output.str();
    check(all.find("没有符合条件的商品") != std::string::npos, "empty tables display a useful explanation");
    check(all.find("SUPERMARKET") != std::string::npos && all.find("12.50") != std::string::npos,
          "page and metric content remain readable in plain output");
    check(all.find('\x1b') == std::string::npos && all.find('\f') == std::string::npos,
          "redirected UI emits neither ANSI nor screen clearing controls");
    std::string remaining;
    std::getline(input, remaining);
    check(remaining == "untouched", "redirected pause consumes no input");
}

void consoleLongFields(const fs::path&) {
    std::istringstream input;
    std::ostringstream output;
    ConsoleUi ui(input, output);
    std::string path;
    for (int index = 0; index < 12; ++index) path += "中文目录/abcdef0123456789/";
    ui.field("PATH", path);
    std::string compact;
    for (const auto ch : output.str()) {
        if (ch != ' ' && ch != '\n' && ch != '\r') compact += ch;
    }
    check(compact == "PATH" + path, "long field wraps while preserving every UTF-8 byte of its value");
    std::istringstream lines(output.str());
    std::string line;
    std::size_t lineCount = 0;
    while (std::getline(lines, line)) {
        ++lineCount;
        check(ConsoleUi::displayWidth(line) <= ui.width(), "wrapped field stays within the available width");
    }
    check(lineCount > 1, "long field actually spans multiple lines");
}

void menuInterfaceWorkflow(const fs::path& path) {
    Fixture f(path);
    const auto milk = f.addStock(10, draft("690001", "原味牛奶"));
    const auto bread = f.addStock(3, draft("690002", "Wholegrain 燕麦面包家庭装"));
    std::istringstream input(std::string("admin\n") + password +
        "\n1\n1\n\n0\n3\n1\n" + milk + "\n2\n3\n4\n10.00\ny\n6\n\n\n0\n0\n");
    std::ostringstream output;
    Menu menu(f.files, input, output);
    menu.run();
    const auto text = output.str();
    for (const auto* marker : {"SUPERMARKET", "经营工作台", "商品中心", "原味牛奶",
                               "销售收银", "当前购物车", "销售小票", "应收", "实收", "找零"}) {
        check(text.find(marker) != std::string::npos, "menu displays expected workflow content");
    }
    check(text.find("5.00") != std::string::npos && text.find("10.00") != std::string::npos,
          "receipt displays exact payment and change amounts");
    check(text.find('\x1b') == std::string::npos && text.find('\f') == std::string::npos,
          "redirected menu emits neither ANSI nor screen clearing controls");
    check(text.find("输入已结束") == std::string::npos && text.find("操作失败") == std::string::npos,
          "existing input sequence completes without extra pause input");
    check(f.files.data().sales.size() == 1 && f.files.data().sales.front().total == 500,
          "interface preserves checkout operation and exact total");
    check(f.products.findProduct(milk).stockQuantity == 8 &&
          f.products.findProduct(bread).stockQuantity == 3,
          "interface deducts only purchased stock");
}

void menuEndOfInput(const fs::path& path) {
    Fixture f(path / "existing");
    const auto id = f.addStock(5);
    const auto before = describe(f.files.data());
    std::istringstream input(std::string("admin\n") + password + "\n\n3\n1\n" + id + "\n1\n");
    std::ostringstream output;
    Menu menu(f.files, input, output);
    menu.run();
    check(output.str().find("输入无效") != std::string::npos, "blank menu choice handled");
    check(output.str().find("输入已结束") != std::string::npos, "EOF leaves nested menu");
    check(describe(f.files.data()) == before, "EOF discards unsubmitted cart without mutation");
    FileManager blank(path / "blank");
    std::istringstream truncated("admin\n");
    std::ostringstream bootstrapOutput;
    Menu bootstrap(blank, truncated, bootstrapOutput);
    bootstrap.run();
    check(blank.data().users.empty(), "EOF during bootstrap creates no account");
    check(bootstrapOutput.str().find("输入已结束") != std::string::npos, "bootstrap EOF exits");
}

fs::path makeRunDirectory() {
    fs::path buildDirectory;
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw Failure("cannot locate test executable");
    buildDirectory = fs::path(std::wstring(buffer.data(), length)).parent_path();
#else
    std::error_code error;
    const auto executable = fs::read_symlink("/proc/self/exe", error);
    buildDirectory = error ? fs::current_path() / "build" : executable.parent_path();
#endif
    const auto base = buildDirectory / "test-data";
    fs::create_directories(base);
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const auto ticks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto path = base / ("run_" + std::to_string(ticks) + "_" + std::to_string(random()));
        if (fs::create_directory(path)) return path;
    }
    throw Failure("cannot create unique test run directory");
}

} // namespace

int main() {
    try {
        const auto root = makeRunDirectory();
        sharedHash = market::hashPassword(password);
        using Case = std::pair<const char*, std::function<void(const std::filesystem::path&)>>;
        const std::vector<Case> cases = {
            {"MoneyParsing", moneyParsing}, {"IntegerParsing", integerParsing},
            {"CalendarValidation", calendarValidation}, {"Utf8Validation", utf8Validation},
            {"HashCollisions", hashCollisions}, {"HashGrowthAndCopy", hashGrowthAndCopy},
            {"PasswordStorage", passwordStorage}, {"FirstAdministrator", firstAdministrator},
            {"AccountLifecycle", accountLifecycle}, {"PasswordChanges", passwordChanges},
            {"CashierPermissions", cashierPermissions}, {"LoggedOutServices", loggedOutServices},
            {"ProductValidation", productValidation}, {"SoftDeletion", softDeletion},
            {"ProcurementBoundaries", procurementBoundaries}, {"CartMergeAndRemoval", cartMergeAndRemoval},
            {"MalformedCarts", malformedCarts}, {"CheckoutAtomicValidation", checkoutAtomicValidation},
            {"CheckoutCostOverflow", checkoutCostOverflow},
            {"HistoricalSnapshots", historicalSnapshots}, {"StocktakeAudit", stocktakeAudit},
            {"DatedReports", datedReports}, {"OwnSalesVisibility", ownSalesVisibility},
            {"InvalidCommitAtomicity", invalidCommitAtomicity}, {"PersistenceRoundTrip", persistenceRoundTrip},
            {"CorruptHeader", corruptHeader}, {"CorruptInventory", corruptInventory},
            {"CorruptTrailingData", corruptTrailingData}, {"DirectoryLocking", directoryLocking},
            {"BackupsRestore", backupsRestore}, {"FailedSavePreservesCheckout", failedSavePreservesCheckout},
            {"ConsoleTextWidths", consoleTextWidths}, {"ConsolePlainRendering", consolePlainRendering},
            {"ConsoleLongFields", consoleLongFields}, {"MenuInterfaceWorkflow", menuInterfaceWorkflow},
            {"MenuEndOfInput", menuEndOfInput}
        };
        std::size_t failed = 0;
        for (const auto& test : cases) {
            try {
                const auto directory = root / test.first;
                if (!std::filesystem::create_directory(directory)) throw Failure("case directory already exists");
                test.second(directory);
                std::cout << "PASS " << test.first << '\n';
            } catch (const Failure& error) {
                ++failed;
                std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
            } catch (const std::exception& error) {
                ++failed;
                std::cerr << "FAIL " << test.first << ": unexpected exception: " << error.what() << '\n';
            }
        }
        std::cout << "RESULT " << cases.size() - failed << '/' << cases.size()
                  << " cases passed; " << assertions << " assertions; " << failed << " failures\n";
        std::cout << "TEST_DATA " << root.u8string() << '\n';
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "FAIL TestSetup: " << error.what() << '\n';
        return 1;
    }
}
