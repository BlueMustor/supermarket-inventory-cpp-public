#include "supermarket/services.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace market {
namespace {

constexpr std::size_t maxRecords = 100'000;
constexpr std::uintmax_t maxFileSize = 256ULL * 1024 * 1024;
constexpr std::size_t maxLineSize = 16'384;

[[noreturn]] void invalid(const std::string& message) {
    throw Error("数据文件无效：" + message);
}

void usernameField(std::string_view name) {
    if (name.size() < 3 || name.size() > 32) invalid("用户名长度必须为 3 至 32 字节。");
    for (char ch : name) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_')) invalid("用户名只能包含英文字母、数字和下划线。");
    }
}

void moneyField(Money value) {
    if (value < 0 || value > maxMoney) invalid("金额超出允许范围。");
}

void quantityField(Quantity value, bool positive = false) {
    if (value < (positive ? 1 : 0) || value > maxQuantity) invalid("数量超出允许范围。");
}

void timestampField(const std::string& timestamp) {
    if (timestamp.size() != 19 || timestamp[10] != ' ' || timestamp[13] != ':' || timestamp[16] != ':') {
        invalid("时间戳格式必须为 YYYY-MM-DD HH:MM:SS。");
    }
    validateDate(std::string_view(timestamp).substr(0, 10));
    const auto pair = [&](std::size_t position, int maximum) {
        if (timestamp[position] < '0' || timestamp[position] > '9' ||
            timestamp[position + 1] < '0' || timestamp[position + 1] > '9' ||
            (timestamp[position] - '0') * 10 + timestamp[position + 1] - '0' > maximum) {
            invalid("时间戳时分秒超出范围。");
        }
    };
    pair(11, 23);
    pair(14, 59);
    pair(17, 59);
}

std::uint64_t idNumber(std::string_view id, char prefix) {
    if (id.size() < 7 || id.front() != prefix) invalid("商品或流水编号格式错误。");
    const auto digits = id.substr(1);
    std::uint64_t value = 0;
    const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (converted.ec != std::errc{} || converted.ptr != digits.data() + digits.size() || value == 0 ||
        (digits.size() > 6 && digits.front() == '0')) invalid("商品或流水编号格式错误。");
    return value;
}

struct Row {
    explicit Row(std::istream& source) {
        std::string line;
        if (!std::getline(source, line)) invalid("文件意外结束。");
        if (line.size() > maxLineSize) invalid("单行长度超出限制。");
        stream.str(std::move(line));
        stream.imbue(std::locale::classic());
    }

    void tag(std::string_view expected) {
        std::string actual;
        if (!(stream >> actual) || actual != expected) invalid("记录类型错误，期望 " + std::string(expected) + "。");
    }

    std::string quoted() {
        stream >> std::ws;
        if (stream.peek() != '"') invalid("文本字段必须使用双引号。");
        std::string value;
        if (!(stream >> std::quoted(value))) invalid("文本字段引号不完整。");
        return value;
    }

    std::string token() {
        std::string value;
        if (!(stream >> value)) invalid("缺少字段。");
        return value;
    }

    std::uint64_t number(std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max()) {
        const auto word = token();
        if (word.empty() || word.front() < '0' || word.front() > '9') invalid("数值字段必须为非负十进制整数。");
        std::uint64_t value = 0;
        const auto result = std::from_chars(word.data(), word.data() + word.size(), value);
        if (result.ec != std::errc{} || result.ptr != word.data() + word.size() || value > maximum) {
            invalid("数值字段格式错误或超出范围。");
        }
        return value;
    }

    Money money() { return static_cast<Money>(number(static_cast<std::uint64_t>(maxMoney))); }
    Quantity quantity() { return static_cast<Quantity>(number(static_cast<std::uint64_t>(maxQuantity))); }
    bool boolean() { return number(1) != 0; }
    void end() {
        stream >> std::ws;
        if (!stream.eof()) invalid("记录包含多余字段。");
    }

    std::istringstream stream;
};

std::size_t sectionCount(std::istream& source, std::string_view section) {
    Row row(source);
    row.tag(section);
    const auto count = static_cast<std::size_t>(row.number(maxRecords));
    row.end();
    return count;
}

std::string uniqueSuffix() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto tick = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::to_string(tick) + "_" + std::to_string(sequence.fetch_add(1));
}

struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

void replaceFile(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw Error("保存失败，无法替换数据文件（系统错误 " + std::to_string(GetLastError()) + "）；原有数据仍保留。");
    }
#else
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) throw Error("保存失败，无法替换数据文件：" + error.message());
#endif
}

}

struct FileManager::Lock {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit Lock(const std::filesystem::path& path) {
        handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) {
                throw Error("该数据目录正在被另一个程序使用，请关闭其他实例后重试。");
            }
            throw Error("无法锁定数据目录（系统错误 " + std::to_string(code) + "）。");
        }
    }
    ~Lock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    int descriptor = -1;
    explicit Lock(const std::filesystem::path& path) {
        descriptor = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (descriptor < 0) throw Error("无法打开数据目录锁文件。");
        if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor);
            descriptor = -1;
            throw Error("该数据目录正在被另一个程序使用，或无法取得排他锁。");
        }
    }
    ~Lock() { if (descriptor >= 0) ::close(descriptor); }
#endif
};

FileManager::FileManager(std::filesystem::path dataDirectory) {
    try {
        std::filesystem::create_directories(dataDirectory);
        dataDirectory_ = std::filesystem::canonical(dataDirectory);
        lock_ = std::make_unique<Lock>(dataDirectory_ / ".market.lock");
        loadData();
    } catch (const std::filesystem::filesystem_error& error) {
        throw Error("无法访问数据目录：" + std::string(error.what()));
    }
}

FileManager::~FileManager() = default;

const State& FileManager::data() const noexcept { return state_; }
const std::filesystem::path& FileManager::directory() const noexcept { return dataDirectory_; }

void FileManager::loadData() {
    const auto path = dataDirectory_ / "store.db";
    if (!std::filesystem::exists(path)) return;
    State loaded = readSnapshot(path);
    validate(loaded);
    using std::swap;
    swap(state_, loaded);
}

void FileManager::commit(State next) {
    validate(next);
    TemporaryFile temporary{dataDirectory_ / ("store.db.tmp_" + uniqueSuffix())};
    writeSnapshot(temporary.path, next);
    replaceFile(temporary.path, dataDirectory_ / "store.db");
    static_assert(std::is_nothrow_swappable_v<State>, "State must support a nonthrowing commit swap");
    using std::swap;
    swap(state_, next);
}

std::filesystem::path FileManager::backupData(const User& user) const {
    user.requirePermission(Permission::Backup);
    const auto source = dataDirectory_ / "store.db";
    const auto backupDirectory = dataDirectory_ / "backups";
    try {
        if (!std::filesystem::is_regular_file(source)) throw Error("还没有可备份的已保存数据。");
        std::filesystem::create_directories(backupDirectory);
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto destination = backupDirectory / ("store_" + uniqueSuffix() + ".db");
            std::error_code error;
            if (std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error)) return destination;
            if (error != std::errc::file_exists) throw Error("备份失败：" + error.message());
        }
        throw Error("无法生成不重名的备份文件。");
    } catch (const std::filesystem::filesystem_error& error) {
        throw Error("备份失败：" + std::string(error.what()));
    }
}

void FileManager::validate(const State& state) {
    if (state.products.size() > maxRecords || state.users.size() > maxRecords ||
        state.purchases.size() > maxRecords || state.sales.size() > maxRecords || state.stocktakes.size() > maxRecords) {
        invalid("记录数量超过 100000 条限制。");
    }
    if (state.nextProductId == 0 || state.nextRecordId == 0) invalid("编号计数器必须为正数。");
    std::unordered_set<std::string> usernames;
    bool enabledAdmin = false;
    for (const auto& account : state.users) {
        usernameField(account.username);
        if (!usernames.insert(account.username).second) invalid("用户名重复。");
        if (!validPasswordHash(account.passwordHash)) invalid("密码哈希格式不受支持。");
        if (account.role != Role::Admin && account.role != Role::Cashier) invalid("账号角色无效。");
        enabledAdmin = enabledAdmin || (account.role == Role::Admin && account.enabled);
    }
    if (!state.users.empty() && !enabledAdmin) invalid("必须保留至少一个启用的管理员。");
    if (state.users.empty() && (state.products.size() != 0 || !state.purchases.empty() ||
                                !state.sales.empty() || !state.stocktakes.empty())) invalid("业务数据缺少账号。");

    std::unordered_set<std::string> barcodes;
    std::unordered_map<std::string, Quantity> inventory;
    const auto products = state.products.entries();
    for (const auto& product : products) {
        if (idNumber(product.productId, 'P') >= state.nextProductId) invalid("商品编号计数器未超过现有编号。");
        if (!inventory.emplace(product.productId, 0).second) invalid("商品编号重复。");
        requireText(product.barcode, "条码", 64);
        requireText(product.name, "商品名");
        requireText(product.category, "分类", 60);
        if (product.barcode.front() == 'P' && product.barcode.size() > 1 &&
            std::all_of(product.barcode.begin() + 1, product.barcode.end(), [](char ch) {
                return ch >= '0' && ch <= '9';
            })) invalid("P 加纯数字保留作商品编号，不能用作条码。");
        if (!barcodes.insert(product.barcode).second) invalid("商品条码重复。");
        moneyField(product.purchasePrice);
        moneyField(product.salePrice);
        quantityField(product.stockQuantity);
        quantityField(product.minStock);
        if (!product.active && product.stockQuantity != 0) invalid("停用商品仍有库存。");
    }

    struct Event { char type; std::size_t index; };
    std::map<std::uint64_t, Event> events;
    const auto record = [&](const std::string& id, char prefix, const std::string& timestamp,
                            const std::string& operatorName, std::size_t index) {
        const auto sequence = idNumber(id, prefix);
        if (sequence >= state.nextRecordId) invalid("流水计数器未超过现有编号。");
        if (!events.emplace(sequence, Event{prefix, index}).second) invalid("全局流水编号重复。");
        timestampField(timestamp);
        if (usernames.find(operatorName) == usernames.end()) invalid("历史操作人账号不存在。");
    };
    const auto productReference = [&](const std::string& id, const std::string& historicalName) {
        if (inventory.find(id) == inventory.end()) invalid("流水引用的商品不存在。");
        requireText(historicalName, "历史商品名");
    };
    for (std::size_t index = 0; index < state.purchases.size(); ++index) {
        const auto& purchase = state.purchases[index];
        record(purchase.id, 'I', purchase.timestamp, purchase.operatorName, index);
        productReference(purchase.productId, purchase.productName);
        quantityField(purchase.quantity, true);
        moneyField(purchase.unitPrice);
        moneyField(purchase.totalCost);
        if (checkedMultiply(purchase.unitPrice, purchase.quantity) != purchase.totalCost) invalid("采购金额与明细不一致。");
    }
    std::size_t totalItems = 0;
    for (std::size_t index = 0; index < state.sales.size(); ++index) {
        const auto& sale = state.sales[index];
        record(sale.id, 'S', sale.timestamp, sale.operatorName, index);
        if (sale.items.empty() || sale.items.size() > maxRecords - totalItems) invalid("销售明细为空或总数量超过限制。");
        totalItems += sale.items.size();
        Money total = 0;
        Money totalCost = 0;
        std::unordered_set<std::string> itemProducts;
        for (const auto& item : sale.items) {
            productReference(item.productId, item.productName);
            if (!itemProducts.insert(item.productId).second) invalid("同一销售单含重复商品行。");
            quantityField(item.quantity, true);
            moneyField(item.unitPrice);
            moneyField(item.unitCost);
            totalCost = checkedAdd(totalCost, checkedMultiply(item.unitCost, item.quantity));
            total = checkedAdd(total, checkedMultiply(item.unitPrice, item.quantity));
        }
        moneyField(sale.total);
        moneyField(sale.paid);
        moneyField(sale.change);
        if (sale.total != total || sale.paid < sale.total || sale.change != sale.paid - sale.total) {
            invalid("销售总额、收款或找零不一致。");
        }
    }
    for (std::size_t index = 0; index < state.stocktakes.size(); ++index) {
        const auto& stocktake = state.stocktakes[index];
        record(stocktake.id, 'T', stocktake.timestamp, stocktake.operatorName, index);
        productReference(stocktake.productId, stocktake.productName);
        quantityField(stocktake.beforeQuantity);
        quantityField(stocktake.countedQuantity);
        requireText(stocktake.reason, "盘点原因", 240);
    }

    // 全局流水次序让库存可以从零重放，避免文件中的库存和业务记录各说各话。
    for (const auto& entry : events) {
        const auto event = entry.second;
        if (event.type == 'I') {
            const auto& purchase = state.purchases[event.index];
            auto& quantity = inventory.at(purchase.productId);
            if (purchase.quantity > maxQuantity - quantity) invalid("采购后的历史库存超出范围。");
            quantity += purchase.quantity;
        } else if (event.type == 'S') {
            for (const auto& item : state.sales[event.index].items) {
                auto& quantity = inventory.at(item.productId);
                if (item.quantity > quantity) invalid("历史销售造成负库存。");
                quantity -= item.quantity;
            }
        } else {
            const auto& stocktake = state.stocktakes[event.index];
            auto& quantity = inventory.at(stocktake.productId);
            if (quantity != stocktake.beforeQuantity) invalid("盘点前库存与历史流水不一致。");
            quantity = stocktake.countedQuantity;
        }
    }
    for (const auto& product : products) {
        if (inventory.at(product.productId) != product.stockQuantity) invalid("商品库存与历史流水不一致。");
    }
}

void FileManager::writeSnapshot(const std::filesystem::path& path, const State& state) {
    try {
        std::ofstream output;
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.open(path, std::ios::binary | std::ios::trunc);
        output.imbue(std::locale::classic());
        output << "MARKET_STORE 1\nCOUNTERS " << state.nextProductId << ' ' << state.nextRecordId
               << "\nUSERS " << state.users.size() << '\n';
        for (const auto& account : state.users) {
            output << "USER " << std::quoted(account.username) << ' ' << std::quoted(account.passwordHash) << ' '
                   << (account.role == Role::Admin ? "ADMIN" : "CASHIER") << ' ' << account.enabled << '\n';
        }
        const auto products = state.products.entries();
        output << "PRODUCTS " << products.size() << '\n';
        for (const auto& product : products) {
            output << "PRODUCT " << std::quoted(product.productId) << ' ' << std::quoted(product.barcode) << ' '
                   << std::quoted(product.name) << ' ' << std::quoted(product.category) << ' ' << product.purchasePrice << ' '
                   << product.salePrice << ' ' << product.stockQuantity << ' ' << product.minStock << ' ' << product.active << '\n';
        }
        output << "PURCHASES " << state.purchases.size() << '\n';
        for (const auto& purchase : state.purchases) {
            output << "PURCHASE " << std::quoted(purchase.id) << ' ' << std::quoted(purchase.timestamp) << ' '
                   << std::quoted(purchase.operatorName) << ' ' << std::quoted(purchase.productId) << ' '
                   << std::quoted(purchase.productName) << ' ' << purchase.quantity << ' ' << purchase.unitPrice << ' '
                   << purchase.totalCost << '\n';
        }
        output << "SALES " << state.sales.size() << '\n';
        for (const auto& sale : state.sales) {
            output << "SALE " << std::quoted(sale.id) << ' ' << std::quoted(sale.timestamp) << ' '
                   << std::quoted(sale.operatorName) << ' ' << sale.total << ' ' << sale.paid << ' '
                   << sale.change << ' ' << sale.items.size() << '\n';
            for (const auto& item : sale.items) {
                output << "ITEM " << std::quoted(item.productId) << ' ' << std::quoted(item.productName) << ' '
                       << item.quantity << ' ' << item.unitPrice << ' ' << item.unitCost << '\n';
            }
        }
        output << "STOCKTAKES " << state.stocktakes.size() << '\n';
        for (const auto& stocktake : state.stocktakes) {
            output << "STOCKTAKE " << std::quoted(stocktake.id) << ' ' << std::quoted(stocktake.timestamp) << ' '
                   << std::quoted(stocktake.operatorName) << ' ' << std::quoted(stocktake.productId) << ' '
                   << std::quoted(stocktake.productName) << ' ' << stocktake.beforeQuantity << ' '
                   << stocktake.countedQuantity << ' ' << std::quoted(stocktake.reason) << '\n';
        }
        output << "END\n";
        output.flush();
        output.close();
    } catch (const std::ios_base::failure&) {
        throw Error("写入数据文件失败；请检查磁盘空间和目录写入权限，原有数据仍保留。");
    }
}

State FileManager::readSnapshot(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maxFileSize) {
        invalid("存储文件类型错误或超过 256 MiB 限制。");
    }
    std::ifstream input(path, std::ios::binary);
    input.imbue(std::locale::classic());
    if (!input) throw Error("无法读取现有数据文件。");
    State state;
    {
        Row row(input);
        row.tag("MARKET_STORE");
        if (row.number() != 1) invalid("存储版本不受支持。");
        row.end();
    }
    {
        Row row(input);
        row.tag("COUNTERS");
        state.nextProductId = row.number();
        state.nextRecordId = row.number();
        row.end();
    }
    const auto users = sectionCount(input, "USERS");
    state.users.reserve(users);
    for (std::size_t i = 0; i < users; ++i) {
        Row row(input);
        row.tag("USER");
        Account account;
        account.username = row.quoted();
        account.passwordHash = row.quoted();
        const auto role = row.token();
        if (role == "ADMIN") account.role = Role::Admin;
        else if (role == "CASHIER") account.role = Role::Cashier;
        else invalid("账号角色无效。");
        account.enabled = row.boolean();
        row.end();
        state.users.push_back(std::move(account));
    }
    const auto products = sectionCount(input, "PRODUCTS");
    for (std::size_t i = 0; i < products; ++i) {
        Row row(input);
        row.tag("PRODUCT");
        Product product;
        product.productId = row.quoted();
        product.barcode = row.quoted();
        product.name = row.quoted();
        product.category = row.quoted();
        product.purchasePrice = row.money();
        product.salePrice = row.money();
        product.stockQuantity = row.quantity();
        product.minStock = row.quantity();
        product.active = row.boolean();
        row.end();
        if (state.products.find(product.productId) != nullptr) invalid("商品编号重复。");
        state.products.insert(std::move(product));
    }
    const auto purchases = sectionCount(input, "PURCHASES");
    state.purchases.reserve(purchases);
    for (std::size_t i = 0; i < purchases; ++i) {
        Row row(input);
        row.tag("PURCHASE");
        PurchaseRecord purchase;
        purchase.id = row.quoted();
        purchase.timestamp = row.quoted();
        purchase.operatorName = row.quoted();
        purchase.productId = row.quoted();
        purchase.productName = row.quoted();
        purchase.quantity = row.quantity();
        purchase.unitPrice = row.money();
        purchase.totalCost = row.money();
        row.end();
        state.purchases.push_back(std::move(purchase));
    }
    const auto sales = sectionCount(input, "SALES");
    state.sales.reserve(sales);
    std::size_t totalItems = 0;
    for (std::size_t i = 0; i < sales; ++i) {
        Row row(input);
        row.tag("SALE");
        SaleRecord sale;
        sale.id = row.quoted();
        sale.timestamp = row.quoted();
        sale.operatorName = row.quoted();
        sale.total = row.money();
        sale.paid = row.money();
        sale.change = row.money();
        const auto items = static_cast<std::size_t>(row.number(maxRecords - totalItems));
        totalItems += items;
        row.end();
        sale.items.reserve(items);
        for (std::size_t itemIndex = 0; itemIndex < items; ++itemIndex) {
            Row itemRow(input);
            itemRow.tag("ITEM");
            SaleLine item;
            item.productId = itemRow.quoted();
            item.productName = itemRow.quoted();
            item.quantity = itemRow.quantity();
            item.unitPrice = itemRow.money();
            item.unitCost = itemRow.money();
            itemRow.end();
            sale.items.push_back(std::move(item));
        }
        state.sales.push_back(std::move(sale));
    }
    const auto stocktakes = sectionCount(input, "STOCKTAKES");
    state.stocktakes.reserve(stocktakes);
    for (std::size_t i = 0; i < stocktakes; ++i) {
        Row row(input);
        row.tag("STOCKTAKE");
        StocktakeRecord stocktake;
        stocktake.id = row.quoted();
        stocktake.timestamp = row.quoted();
        stocktake.operatorName = row.quoted();
        stocktake.productId = row.quoted();
        stocktake.productName = row.quoted();
        stocktake.beforeQuantity = row.quantity();
        stocktake.countedQuantity = row.quantity();
        stocktake.reason = row.quoted();
        row.end();
        state.stocktakes.push_back(std::move(stocktake));
    }
    {
        Row row(input);
        row.tag("END");
        row.end();
    }
    std::string tail;
    while (std::getline(input, tail)) {
        if (tail.find_first_not_of(" \t\r") != std::string::npos) invalid("END 后仍有多余内容。");
    }
    if (!input.eof() || input.bad()) throw Error("读取数据文件时发生输入错误。");
    return state;
}

}
