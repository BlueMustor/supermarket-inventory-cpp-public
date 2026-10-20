#include "supermarket/services.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace market {
namespace {

struct EndOfInput {};

std::string trim(std::string text) {
    const auto whitespace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    const auto first = std::find_if_not(text.begin(), text.end(), whitespace);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), whitespace).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string periodLabel(const std::string& start, const std::string& end) {
    if (start.empty() && end.empty()) return "全部日期";
    return (start.empty() ? "最早记录" : start) + " 至 " + (end.empty() ? "最新记录" : end);
}

class PasswordEchoGuard {
public:
    explicit PasswordEchoGuard(bool useConsole) {
#ifdef _WIN32
        if (useConsole) {
            handle_ = GetStdHandle(STD_INPUT_HANDLE);
            if (handle_ != INVALID_HANDLE_VALUE && GetConsoleMode(handle_, &mode_)) {
                changed_ = SetConsoleMode(handle_, mode_ & ~ENABLE_ECHO_INPUT) != 0;
            }
        }
#else
        (void)useConsole;
#endif
    }

    ~PasswordEchoGuard() {
#ifdef _WIN32
        if (changed_) SetConsoleMode(handle_, mode_);
#endif
    }

    bool changed() const noexcept {
#ifdef _WIN32
        return changed_;
#else
        return false;
#endif
    }

    PasswordEchoGuard(const PasswordEchoGuard&) = delete;
    PasswordEchoGuard& operator=(const PasswordEchoGuard&) = delete;

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD mode_ = 0;
    bool changed_ = false;
#endif
};

}

Menu::Menu(FileManager& files, std::istream& input, std::ostream& output)
    : files_(files), input_(input), output_(output), ui_(input, output), user_(files),
      products_(files, user_), purchases_(files, user_, products_),
      sales_(files, user_, products_), inventory_(files, user_, products_),
      reports_(files, user_) {}

std::string Menu::readLine(const std::string& prompt) {
    ui_.prompt(prompt);
    std::string line;
    if (!std::getline(input_, line)) throw EndOfInput{};
    if (!ui_.interactive()) output_ << '\n';
    return trim(std::move(line));
}

std::string Menu::readPassword(const std::string& prompt) {
    ui_.prompt(prompt);
    PasswordEchoGuard echoGuard(&input_ == &std::cin);
    std::string password;
    if (!std::getline(input_, password)) throw EndOfInput{};
    if (!password.empty() && password.back() == '\r') password.pop_back();
    if (echoGuard.changed() || !ui_.interactive()) output_ << '\n';
    return password;
}

std::int64_t Menu::readNumber(const std::string& prompt, std::int64_t minimum, std::int64_t maximum) {
    for (;;) {
        try {
            return parseInteger(readLine(prompt), minimum, maximum);
        } catch (const Error& error) {
            ui_.message("输入无效：" + std::string(error.what()), Tone::Bad);
        }
    }
}

Money Menu::readMoney(const std::string& prompt) {
    for (;;) {
        try {
            return parseMoney(readLine(prompt));
        } catch (const Error& error) {
            ui_.message("金额无效：" + std::string(error.what()), Tone::Bad);
            ui_.message("例如 12.50，最多两位小数。", Tone::Muted);
        }
    }
}

bool Menu::confirm(const std::string& prompt) {
    auto answer = readLine(prompt + " [y 确认 / 其他取消]：");
    std::transform(answer.begin(), answer.end(), answer.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return answer == "y" || answer == "yes";
}

void Menu::showPage(const std::string& title, const std::string& description) {
    ui_.page(title, description);
    if (user_.isLoggedIn()) {
        const auto& account = user_.current();
        ui_.field("当前账号", account.username + "  /  " + roleName(account.role), Tone::Muted);
    }
}

void Menu::run() {
    try {
        while (!user_.hasUsers()) {
            showPage("欢迎使用", "首次启动 · 创建管理员后，即可开始管理门店业务");
            ui_.section("设置管理员账号");
            ui_.message("用户名使用 3 至 32 位英文字母、数字或下划线；输入 0 退出。");
            const auto username = readLine("管理员用户名：");
            if (username == "0") return;
            try {
                const auto password = readPassword("设置密码（8 至 128 字节）：");
                const auto repeated = readPassword("再次输入密码：");
                if (password != repeated) throw Error("两次密码不一致，请重新创建。");
                if (confirm("创建管理员 " + username + "？")) {
                    user_.bootstrapAdmin(username, password);
                    ui_.message("管理员已创建，请登录。", Tone::Good);
                } else ui_.message("已取消创建。");
            } catch (const Error& error) {
                ui_.message("创建失败：" + std::string(error.what()), Tone::Bad);
            }
            ui_.pause();
        }
        for (;;) {
            if (!user_.isLoggedIn()) {
                showPage("账号登录", "欢迎回来 · 登录后进入你的经营工作台");
                ui_.section("登录");
                ui_.message("请输入账号和密码，用户名输入 0 退出。");
                const auto username = readLine("用户名：");
                if (username == "0") break;
                const auto password = readPassword("密码：");
                try {
                    user_.login(username, password);
                } catch (const Error& error) {
                    ui_.message("登录失败：" + std::string(error.what()), Tone::Bad);
                    ui_.pause();
                    continue;
                }
            }
            showMainMenu();
            const auto choice = static_cast<int>(readNumber("输入功能编号：", 0, 9));
            if (choice == 0) break;
            try {
                dispatch(choice);
            } catch (const Error& error) {
                ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
                ui_.pause();
            }
        }
        user_.logout();
        ui_.message("已退出，所有已提交业务均已保存。", Tone::Good);
    } catch (const EndOfInput&) {
        user_.logout();
        ui_.message("输入已结束，程序退出。", Tone::Muted);
    }
}

void Menu::showMainMenu() {
    const bool admin = user_.checkPermission(Permission::ManageProducts);
    const auto today = nowTimestamp().substr(0, 10);
    showPage("经营工作台", today + "  ·  " + (admin ? "门店总览与日常管理" : "收银与商品查询"));
    Money revenue = 0;
    std::size_t tickets = 0;
    bool revenueOverflow = false;
    for (const auto& sale : files_.data().sales) {
        if (sale.timestamp.substr(0, 10) == today && (admin || sale.operatorName == user_.current().username)) {
            ++tickets;
            if (sale.total > maxMoney - revenue) revenueOverflow = true;
            else revenue += sale.total;
        }
    }
    std::size_t active = 0;
    std::size_t lowStock = 0;
    for (const auto& product : files_.data().products.entries()) {
        if (product.active) {
            ++active;
            if (product.stockQuantity <= product.minStock) ++lowStock;
        }
    }
    std::vector<std::pair<std::string, std::string>> metrics = {
        {admin ? "今日销售 / 元" : "本人今日 / 元", revenueOverflow ? "超出统计上限" : formatMoney(revenue)},
        {admin ? "今日订单" : "今日本人订单", std::to_string(tickets)}, {"在售商品", std::to_string(active)}
    };
    if (admin) metrics.push_back({"低库存商品", std::to_string(lowStock)});
    ui_.metrics(metrics);
    ui_.section("常用功能");
    std::vector<MenuOption> options = {{"1", admin ? "商品中心" : "商品查询", "查找商品、价格与库存"}};
    if (admin) options.push_back({"2", "采购入库", "录入采购，增加商品库存"});
    options.push_back({"3", "销售收银", "购物车、收款与销售小票"});
    if (admin) {
        options.push_back({"4", "库存盘点", "核对库存、调整差异与预警"});
        options.push_back({"5", "经营报表", "销售、采购与库存汇总"});
    }
    options.push_back({"6", admin ? "账号管理" : "修改密码", admin ? "管理门店人员与账号状态" : "更新当前账号的登录密码"});
    if (admin) options.push_back({"7", "数据备份", "保存一份完整业务数据副本"});
    options.push_back({"9", "切换账号", "注销后返回登录页"});
    options.push_back({"0", "退出系统", "结束本次使用"});
    ui_.menu(options);
}

void Menu::dispatch(int choice) {
    switch (choice) {
    case 1: productMenu(); break;
    case 2:
        user_.requirePermission(Permission::Purchase);
        purchaseMenu();
        break;
    case 3: saleMenu(); break;
    case 4:
        user_.requirePermission(Permission::Inventory);
        inventoryMenu();
        break;
    case 5:
        user_.requirePermission(Permission::Reports);
        reportMenu();
        break;
    case 6: accountMenu(); break;
    case 7:
        user_.requirePermission(Permission::Backup);
        showPage("数据备份", "保存当前已提交的数据，不包含未结算的购物车");
        if (confirm("创建一份完整备份？")) {
            const auto path = files_.backupData(user_);
            ui_.message("备份已保存。", Tone::Good);
            ui_.field("保存位置", path.u8string());
        } else ui_.message("已取消备份。");
        ui_.pause();
        break;
    case 9:
        user_.logout();
        ui_.message("已注销。", Tone::Good);
        break;
    default:
        ui_.message("没有此菜单项，请重新选择。", Tone::Warning);
        ui_.pause();
    }
}

void Menu::printProducts(const std::vector<Product>& products) {
    const bool admin = user_.checkPermission(Permission::ManageProducts);
    const bool compact = ui_.width() < 96;
    ui_.section("商品列表 · " + std::to_string(products.size()) + " 项");
    std::vector<TableColumn> columns = {{"编号", 9}, {"商品名称", compact ? 18U : 20U}};
    if (admin && !compact) columns.push_back({"参考进价", 10, true});
    columns.push_back({"售价", 10, true});
    columns.push_back({"库存", 8, true});
    if (admin) columns.push_back({"预警值", 6, true});
    if (!compact) columns.push_back({"分类", 10});
    columns.push_back({"状态", 6});
    std::vector<std::vector<std::string>> rows;
    for (const auto& product : products) {
        std::vector<std::string> row = {product.productId, product.name};
        if (admin && !compact) row.push_back(formatMoney(product.purchasePrice));
        row.push_back(formatMoney(product.salePrice));
        row.push_back(std::to_string(product.stockQuantity));
        if (admin) row.push_back(std::to_string(product.minStock));
        if (!compact) row.push_back(product.category);
        row.push_back(!product.active ? "停用" : (admin && product.stockQuantity <= product.minStock ? "预警" : "在售"));
        rows.push_back(std::move(row));
    }
    ui_.table(columns, rows, "没有符合条件的商品，可尝试其他关键词。");
    if (products.size() == 1) {
        ui_.field("完整名称", products.front().name);
        ui_.field("条码", products.front().barcode);
        ui_.field("分类", products.front().category);
        if (admin && compact) ui_.field("参考进价", formatMoney(products.front().purchasePrice) + " 元");
    } else if (!products.empty()) ui_.message("可按编号或条码查询完整商品资料。");
}

void Menu::productMenu() {
    for (;;) {
        const bool admin = user_.checkPermission(Permission::ManageProducts);
        showPage("商品中心", "查询价格与库存，维护在售商品资料");
        std::vector<MenuOption> options = {
            {"1", "商品列表", "按名称、条码或分类筛选"}, {"2", "查询单件商品", "输入编号或条码查看详情"}
        };
        if (admin) {
            options.push_back({"3", "新增商品", "创建商品资料，初始库存为零"});
            options.push_back({"4", "修改商品", "更新价格、分类与预警值"});
            options.push_back({"5", "停用商品", "库存为零时可停用，保留历史"});
        }
        options.push_back({"0", "返回工作台", ""});
        ui_.menu(options);
        const auto choice = readNumber("输入功能编号：", 0, 5);
        if (choice == 0) return;
        showPage("商品中心", "商品资料与库存查询");
        try {
            if (choice == 1) {
                const auto keyword = readLine("搜索关键词（留空显示全部）：");
                printProducts(products_.listProducts(keyword, admin));
            } else if (choice == 2) {
                const auto key = readLine("商品编号或条码：");
                printProducts({products_.findProduct(key)});
            } else if (choice == 3 || choice == 4) {
                user_.requirePermission(Permission::ManageProducts);
                ui_.section(choice == 3 ? "新增商品" : "修改商品");
                ProductDraft draft;
                std::string id;
                if (choice == 4) {
                    const auto product = products_.findProduct(readLine("待修改的商品编号或条码："));
                    printProducts({product});
                    id = product.productId;
                    draft = {product.barcode, product.name, product.category,
                             product.purchasePrice, product.salePrice, product.minStock};
                    ui_.message("字段留空保留原值；库存请通过采购、销售或盘点修改。");
                    const auto barcode = readLine("条码 [" + draft.barcode + " ]：");
                    const auto name = readLine("名称 [" + draft.name + " ]：");
                    const auto category = readLine("分类 [" + draft.category + " ]：");
                    if (!barcode.empty()) draft.barcode = barcode;
                    if (!name.empty()) draft.name = name;
                    if (!category.empty()) draft.category = category;
                    const auto purchasePrice = readLine("参考进价 [" + formatMoney(draft.purchasePrice) + " ]：");
                    const auto salePrice = readLine("售价 [" + formatMoney(draft.salePrice) + " ]：");
                    const auto minStock = readLine("库存预警值 [" + std::to_string(draft.minStock) + " ]：");
                    if (!purchasePrice.empty()) draft.purchasePrice = parseMoney(purchasePrice);
                    if (!salePrice.empty()) draft.salePrice = parseMoney(salePrice);
                    if (!minStock.empty()) draft.minStock = parseInteger(minStock, 0, maxQuantity);
                } else {
                    draft.barcode = readLine("条码：");
                    draft.name = readLine("商品名称：");
                    draft.category = readLine("分类：");
                    draft.purchasePrice = readMoney("参考进价（元）：");
                    draft.salePrice = readMoney("售价（元）：");
                    draft.minStock = readNumber("最低库存预警值：", 0, maxQuantity);
                }
                ui_.section("确认商品资料");
                ui_.field("名称", draft.name);
                ui_.field("条码", draft.barcode);
                ui_.field("分类", draft.category);
                ui_.metrics({{"参考进价 / 元", formatMoney(draft.purchasePrice)},
                             {"售价 / 元", formatMoney(draft.salePrice)}, {"库存预警值", std::to_string(draft.minStock)}});
                if (confirm("确认保存商品资料？")) {
                    if (choice == 3) {
                        const auto productId = products_.addProduct(draft);
                        ui_.message("商品已新增，编号 " + productId + "，初始库存为 0。", Tone::Good);
                    } else {
                        products_.updateProduct(id, draft);
                        ui_.message("商品资料已更新。", Tone::Good);
                    }
                } else ui_.message("已取消保存。");
            } else if (choice == 5) {
                user_.requirePermission(Permission::ManageProducts);
                const auto product = products_.findProduct(readLine("待停用的商品编号或条码："));
                printProducts({product});
                ui_.message("停用后不再参与采购、销售与盘点，历史记录会保留。", Tone::Warning);
                if (confirm("确认停用商品 " + product.name + "？")) {
                    products_.deleteProduct(product.productId);
                    ui_.message("商品已停用，历史记录已保留。", Tone::Good);
                } else ui_.message("已取消停用。");
            }
        } catch (const Error& error) {
            ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
        }
        ui_.pause();
    }
}

void Menu::purchaseMenu() {
    for (;;) {
        showPage("采购入库", "记录实际采购数量与进价，确认后增加库存");
        ui_.menu({{"1", "录入采购", "新增一笔采购并确认入库"},
                  {"2", "采购记录", "按日期查看入库明细"}, {"0", "返回工作台", ""}});
        const auto choice = readNumber("输入功能编号：", 0, 2);
        if (choice == 0) return;
        showPage("采购入库", choice == 1 ? "录入采购" : "查询采购记录");
        try {
            if (choice == 1) {
                const auto product = products_.findProduct(readLine("商品编号或条码："));
                printProducts({product});
                const auto quantity = readNumber("采购数量：", 1, maxQuantity);
                const auto price = readMoney("本次实际采购单价（元）：");
                ui_.metrics({{"采购数量", std::to_string(quantity)}, {"采购单价 / 元", formatMoney(price)},
                             {"采购金额 / 元", formatMoney(checkedMultiply(price, quantity))}});
                if (confirm("确认采购入库？")) {
                    const auto record = purchases_.confirmInbound(product.productId, quantity, price);
                    ui_.message("采购成功，单号 " + record.id + "。", Tone::Good);
                    ui_.field("当前库存", std::to_string(products_.findProduct(product.productId).stockQuantity));
                } else ui_.message("已取消入库。");
            } else {
                const auto start = readLine("开始日期 YYYY-MM-DD（留空不限）：");
                const auto end = readLine("结束日期 YYYY-MM-DD（留空不限）：");
                const auto records = purchases_.queryPurchase(start, end);
                ui_.field("查询区间", periodLabel(start, end));
                ui_.section("采购记录 · " + std::to_string(records.size()) + " 笔");
                std::vector<std::vector<std::string>> rows;
                for (const auto& record : records) {
                    rows.push_back({record.id, record.timestamp, record.productName, std::to_string(record.quantity),
                                    formatMoney(record.unitPrice), formatMoney(record.totalCost), record.operatorName});
                }
                ui_.table({{"单号", 9}, {"入库时间", 19}, {"商品", 18}, {"数量", 7, true},
                           {"单价", 10, true}, {"金额", 12, true}, {"操作人", 12}}, rows, "当前区间没有采购记录。");
            }
        } catch (const Error& error) {
            ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
        }
        ui_.pause();
    }
}

void Menu::printSales(const std::vector<SaleRecord>& records) {
    if (records.empty()) {
        ui_.message("当前区间没有销售记录。");
        return;
    }
    for (const auto& record : records) {
        ui_.section("销售小票  /  " + record.id);
        ui_.field("交易时间", record.timestamp);
        ui_.field("收银员", record.operatorName);
        std::vector<std::vector<std::string>> rows;
        for (const auto& item : record.items) {
            rows.push_back({item.productId, item.productName, std::to_string(item.quantity),
                            formatMoney(item.unitPrice), formatMoney(checkedMultiply(item.unitPrice, item.quantity))});
        }
        ui_.table({{"商品编号", 10}, {"商品名称", 30}, {"数量", 8, true},
                   {"单价 / 元", 14, true}, {"小计 / 元", 16, true}}, rows);
        ui_.metrics({{"应收 / 元", formatMoney(record.total)}, {"实收 / 元", formatMoney(record.paid)},
                     {"找零 / 元", formatMoney(record.change)}});
        ui_.divider();
    }
}

void Menu::printCart(const std::vector<CartItem>& cart) {
    ui_.section("当前购物车");
    std::vector<std::vector<std::string>> rows;
    for (const auto& item : cart) {
        const auto product = products_.findProduct(item.productId);
        rows.push_back({product.productId, product.name, std::to_string(item.quantity),
                        formatMoney(product.salePrice), formatMoney(checkedMultiply(product.salePrice, item.quantity))});
    }
    ui_.table({{"商品编号", 10}, {"商品名称", 30}, {"数量", 8, true},
               {"单价 / 元", 14, true}, {"小计 / 元", 16, true}}, rows, "购物车为空，选择 1 扫码或输入商品编号加购。");
    if (!cart.empty()) ui_.field("应收合计", formatMoney(sales_.quote(cart)) + " 元", Tone::Accent);
}

void Menu::saleMenu() {
    std::vector<CartItem> cart;
    for (;;) {
        showPage("销售收银", "加购商品 → 核对应收 → 收款结算");
        if (!cart.empty()) {
            ui_.metrics({{"购物车品种", std::to_string(cart.size())}, {"待收金额 / 元", formatMoney(sales_.quote(cart))}});
        }
        ui_.menu({{"1", "加入购物车", "输入商品编号或条码"}, {"2", "移除商品", "移除该商品整行"},
                  {"3", "查看购物车", "检查数量、单价与小计"}, {"4", "收款结算", "确认实收金额并生成小票"},
                  {"5", "清空购物车", "放弃当前未提交商品"}, {"6", "销售记录", "收银员只显示本人记录"},
                  {"0", "返回工作台", "未结算购物车需确认丢弃"}});
        const auto choice = readNumber("输入功能编号：", 0, 6);
        if (choice == 0) {
            if (cart.empty() || confirm("购物车未结算，确认丢弃并返回工作台？")) return;
            continue;
        }
        showPage("销售收银", "购物车与收银结算");
        try {
            if (choice == 1) {
                const auto product = products_.findProduct(readLine("商品编号或条码："));
                printProducts({product});
                const auto quantity = readNumber("加入数量：", 1, maxQuantity);
                sales_.addToCart(cart, product.productId, quantity);
                ui_.message("已加入购物车。", Tone::Good);
                printCart(cart);
            } else if (choice == 2) {
                const auto key = readLine("待移除的商品编号或条码：");
                sales_.removeFromCart(cart, key);
                ui_.message("已从购物车移除该商品。", Tone::Good);
                printCart(cart);
            } else if (choice == 3) {
                printCart(cart);
            } else if (choice == 4) {
                if (cart.empty()) throw Error("购物车为空，请先加入商品。");
                printCart(cart);
                const auto total = sales_.quote(cart);
                const auto paid = readMoney("实收金额（元）：");
                if (paid < total) throw Error("实收金额不足，购物车已保留。");
                ui_.metrics({{"应收 / 元", formatMoney(total)}, {"实收 / 元", formatMoney(paid)},
                             {"找零 / 元", formatMoney(paid - total)}});
                if (confirm("确认已收款并结算？")) {
                    const auto receipt = sales_.checkout(cart, paid);
                    showPage("结算完成", "款项已确认，库存与销售记录已保存");
                    ui_.message("结算成功。", Tone::Good);
                    printSales({receipt});
                } else ui_.message("已取消结算，购物车已保留。");
            } else if (choice == 5) {
                if (confirm("确认清空购物车？")) {
                    cart.clear();
                    ui_.message("购物车已清空。", Tone::Good);
                } else ui_.message("已保留购物车。");
            } else {
                const auto start = readLine("开始日期 YYYY-MM-DD（留空不限）：");
                const auto end = readLine("结束日期 YYYY-MM-DD（留空不限）：");
                const auto records = sales_.querySale(start, end);
                ui_.field("查询区间", periodLabel(start, end));
                ui_.field("销售笔数", std::to_string(records.size()));
                printSales(records);
            }
        } catch (const Error& error) {
            ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
        }
        ui_.pause();
    }
}

void Menu::inventoryMenu() {
    for (;;) {
        showPage("库存盘点", "核对实物、记录差异，追踪低库存商品");
        ui_.menu({{"1", "记录盘点", "填写实盘数量及差异原因"}, {"2", "低库存预警", "查看达到预警值的商品"},
                  {"3", "盘点历史", "核对调整前后数量与说明"}, {"0", "返回工作台", ""}});
        const auto choice = readNumber("输入功能编号：", 0, 3);
        if (choice == 0) return;
        showPage("库存盘点", "盘点记录与库存预警");
        try {
            if (choice == 1) {
                const auto product = products_.findProduct(readLine("商品编号或条码："));
                printProducts({product});
                const auto counted = readNumber("实盘数量：", 0, maxQuantity);
                const auto reason = readLine("盘点说明（如损耗、漏记、实物核实）：");
                const auto difference = counted - product.stockQuantity;
                ui_.metrics({{"账面库存", std::to_string(product.stockQuantity)}, {"实盘库存", std::to_string(counted)},
                             {"调整差异", (difference > 0 ? "+" : "") + std::to_string(difference)}});
                ui_.field("盘点说明", reason);
                if (confirm("确认记录盘点并调整库存？")) {
                    const auto record = inventory_.countInventory(product.productId, counted, reason);
                    ui_.message("盘点已保存，单号 " + record.id + "。", Tone::Good);
                } else ui_.message("已取消盘点。");
            } else if (choice == 2) {
                const auto products = inventory_.checkLowStock();
                if (products.empty()) ui_.message("当前没有低库存商品。", Tone::Good);
                else {
                    ui_.message("以下商品库存已达到或低于预警值，请及时补货。", Tone::Warning);
                    printProducts(products);
                }
            } else {
                const auto records = inventory_.records();
                ui_.section("盘点历史 · " + std::to_string(records.size()) + " 笔");
                std::vector<std::vector<std::string>> rows;
                for (const auto& record : records) {
                    rows.push_back({record.id, record.timestamp, record.productName, std::to_string(record.beforeQuantity),
                                    std::to_string(record.countedQuantity), std::to_string(record.countedQuantity - record.beforeQuantity),
                                    record.operatorName});
                }
                ui_.table({{"单号", 9}, {"盘点时间", 19}, {"商品", 20}, {"原库存", 8, true},
                           {"实盘", 8, true}, {"差异", 8, true}, {"操作人", 12}}, rows, "尚无盘点记录。");
                for (const auto& record : records) ui_.field(record.id + " 说明", record.reason);
            }
        } catch (const Error& error) {
            ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
        }
        ui_.pause();
    }
}

void Menu::reportMenu() {
    for (;;) {
        showPage("经营报表", "汇总销售、采购与当前库存，支持日期区间查询");
        ui_.menu({{"1", "销售统计", "收入、参考成本与估算毛利"}, {"2", "采购统计", "采购笔数、数量与金额"},
                  {"3", "当前库存", "在售品种、数量与库存估值"}, {"0", "返回工作台", ""}});
        const auto choice = readNumber("输入功能编号：", 0, 3);
        if (choice == 0) return;
        showPage("经营报表", choice == 3 ? "当前库存汇总" : "日期区间统计");
        try {
            if (choice == 3) {
                const auto report = reports_.inventoryReport();
                ui_.section("当前库存概况");
                ui_.metrics({{"在售商品数", std::to_string(report.activeProducts)}, {"库存总数量", std::to_string(report.totalQuantity)},
                             {"低库存商品数", std::to_string(report.lowStockCount)}});
                ui_.field("按当前参考进价估值", formatMoney(report.costValue) + " 元", Tone::Accent);
                ui_.field("按当前售价估值", formatMoney(report.retailValue) + " 元", Tone::Accent);
                ui_.message("库存估值采用当前价格，当前页面不代表历史日期的库存快照。");
            } else {
                const auto start = readLine("开始日期 YYYY-MM-DD（留空不限）：");
                const auto end = readLine("结束日期 YYYY-MM-DD（留空不限）：");
                ui_.field("统计区间", periodLabel(start, end));
                if (choice == 1) {
                    const auto report = reports_.salesReport(start, end);
                    ui_.section("销售统计");
                    ui_.metrics({{"销售收入 / 元", formatMoney(report.revenue)}, {"销售时参考成本 / 元", formatMoney(report.cost)},
                                 {"估算销售毛利 / 元", formatMoney(report.grossProfit)}});
                    ui_.field("销售笔数", std::to_string(report.transactionCount));
                    ui_.field("销售数量", std::to_string(report.quantity));
                    ui_.message("毛利按销售时参考进价估算，不包含税费、租金等费用。");
                } else {
                    const auto report = reports_.purchaseReport(start, end);
                    ui_.section("采购统计");
                    ui_.metrics({{"采购金额 / 元", formatMoney(report.totalCost)},
                                 {"采购笔数", std::to_string(report.transactionCount)}, {"采购数量", std::to_string(report.quantity)}});
                }
            }
        } catch (const Error& error) {
            ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
        }
        ui_.pause();
    }
}

void Menu::accountMenu() {
    for (;;) {
        const bool admin = user_.checkPermission(Permission::ManageUsers);
        showPage(admin ? "账号管理" : "账号设置", "维护门店账号与登录密码");
        std::vector<MenuOption> options = {{"1", "修改本人密码", "验证原密码后设置新密码"}};
        if (admin) {
            options.push_back({"2", "新增账号", "为管理员或收银员创建账号"});
            options.push_back({"3", "启用 / 停用", "调整账号可用状态"});
            options.push_back({"4", "账号列表", "查看角色和账号状态"});
        }
        options.push_back({"0", "返回工作台", ""});
        ui_.menu(options);
        const auto choice = readNumber("输入功能编号：", 0, 4);
        if (choice == 0) return;
        showPage("账号设置", "密码与人员账号管理");
        try {
            if (choice == 1) {
                ui_.section("修改本人密码");
                const auto oldPassword = readPassword("原密码：");
                const auto newPassword = readPassword("新密码（8 至 128 字节）：");
                const auto repeated = readPassword("再次输入新密码：");
                if (newPassword != repeated) throw Error("两次新密码不一致。");
                if (confirm("确认修改密码？")) {
                    user_.changePassword(oldPassword, newPassword);
                    ui_.message("密码已修改。", Tone::Good);
                } else ui_.message("已取消修改。");
            } else if (choice == 2) {
                user_.requirePermission(Permission::ManageUsers);
                ui_.section("新增账号");
                const auto username = readLine("新用户名：");
                const auto password = readPassword("初始密码（8 至 128 字节）：");
                const auto repeated = readPassword("再次输入密码：");
                if (password != repeated) throw Error("两次密码不一致。");
                const auto roleChoice = readNumber("角色（1 管理员，2 收银员）：", 1, 2);
                const auto role = roleChoice == 1 ? Role::Admin : Role::Cashier;
                ui_.field("账号", username);
                ui_.field("角色", roleName(role));
                if (confirm("确认创建账号？")) {
                    user_.addUser(username, password, role);
                    ui_.message("账号已创建。", Tone::Good);
                } else ui_.message("已取消创建。");
            } else if (choice == 3) {
                user_.requirePermission(Permission::ManageUsers);
                const auto username = readLine("待操作用户名：");
                const bool enabled = readNumber("操作（1 启用，2 停用）：", 1, 2) == 1;
                if (confirm(std::string(enabled ? "确认启用 " : "确认停用 ") + username + "？")) {
                    user_.setEnabled(username, enabled);
                    ui_.message("账号状态已更新。", Tone::Good);
                } else ui_.message("已取消操作。");
            } else {
                const auto accounts = user_.listUsers();
                ui_.section("账号列表 · " + std::to_string(accounts.size()) + " 人");
                std::vector<std::vector<std::string>> rows;
                for (const auto& account : accounts) {
                    rows.push_back({account.username, roleName(account.role), account.enabled ? "启用" : "停用",
                                    account.username == user_.current().username ? "当前登录" : ""});
                }
                ui_.table({{"用户名", 32}, {"角色", 16}, {"状态", 12}, {"备注", 16}}, rows);
            }
        } catch (const Error& error) {
            ui_.message("操作失败：" + std::string(error.what()), Tone::Bad);
        }
        ui_.pause();
    }
}

}
