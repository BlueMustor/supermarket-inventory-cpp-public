#include "supermarket/services.hpp"

#include <algorithm>

namespace market {
namespace {

void validateUsername(std::string_view username) {
    if (username.size() < 3 || username.size() > 32 ||
        !std::all_of(username.begin(), username.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '_';
        })) {
        throw Error("用户名须为 3 至 32 个英文字母、数字或下划线。");
    }
}

const Account* findAccount(const State& state, std::string_view username) {
    const auto found = std::find_if(state.users.begin(), state.users.end(), [&](const Account& account) {
        return account.username == username;
    });
    return found == state.users.end() ? nullptr : &*found;
}

}

User::User(FileManager& files) : files_(files) {}

bool User::hasUsers() const noexcept {
    return !files_.data().users.empty();
}

bool User::isLoggedIn() const noexcept {
    if (!currentUsername_) {
        return false;
    }
    const auto* account = findAccount(files_.data(), *currentUsername_);
    return account != nullptr && account->enabled;
}

void User::bootstrapAdmin(const std::string& username, const std::string& password) {
    if (hasUsers()) {
        throw Error("管理员已初始化，请正常登录。");
    }
    validateUsername(username);
    State next = files_.data();
    next.users.push_back({username, hashPassword(password), Role::Admin, true});
    files_.commit(std::move(next));
}

void User::login(const std::string& username, const std::string& password) {
    logout();
    const auto* account = findAccount(files_.data(), username);
    if (account == nullptr || !account->enabled || !verifyPassword(password, account->passwordHash)) {
        throw Error("用户名或密码错误，或账号已停用。");
    }
    currentUsername_ = username;
}

void User::logout() noexcept {
    currentUsername_.reset();
}

const Account& User::current() const {
    if (!currentUsername_) {
        throw Error("请先登录。");
    }
    const auto* account = findAccount(files_.data(), *currentUsername_);
    if (account == nullptr || !account->enabled) {
        throw Error("当前账号已失效，请重新登录。");
    }
    return *account;
}

bool User::checkPermission(Permission permission) const {
    const auto& account = current();
    switch (permission) {
    case Permission::ManageUsers:
    case Permission::ManageProducts:
    case Permission::Purchase:
    case Permission::Inventory:
    case Permission::Reports:
    case Permission::Backup:
        return account.role == Role::Admin;
    case Permission::Sell:
        return account.role == Role::Admin || account.role == Role::Cashier;
    }
    return false;
}

void User::requirePermission(Permission permission) const {
    if (!checkPermission(permission)) {
        throw Error("当前角色无权执行该操作。");
    }
}

void User::addUser(const std::string& username, const std::string& password, Role role) {
    requirePermission(Permission::ManageUsers);
    validateUsername(username);
    if (role != Role::Admin && role != Role::Cashier) {
        throw Error("无效角色。");
    }
    if (findAccount(files_.data(), username) != nullptr) {
        throw Error("用户名已存在，停用账号的用户名也不能重复使用。");
    }
    State next = files_.data();
    next.users.push_back({username, hashPassword(password), role, true});
    files_.commit(std::move(next));
}

void User::setEnabled(const std::string& username, bool enabled) {
    requirePermission(Permission::ManageUsers);
    if (!enabled && username == current().username) {
        throw Error("不能停用当前登录账号。");
    }
    State next = files_.data();
    auto found = std::find_if(next.users.begin(), next.users.end(), [&](const Account& account) {
        return account.username == username;
    });
    if (found == next.users.end()) {
        throw Error("账号不存在。");
    }
    found->enabled = enabled;
    const bool hasAdmin = std::any_of(next.users.begin(), next.users.end(), [](const Account& account) {
        return account.enabled && account.role == Role::Admin;
    });
    if (!hasAdmin) {
        throw Error("必须保留至少一个启用的管理员。");
    }
    files_.commit(std::move(next));
}

void User::changePassword(const std::string& oldPassword, const std::string& newPassword) {
    const std::string username = current().username;
    if (!verifyPassword(oldPassword, current().passwordHash)) {
        throw Error("原密码错误。");
    }
    if (oldPassword == newPassword) {
        throw Error("新密码不能与原密码相同。");
    }
    State next = files_.data();
    auto found = std::find_if(next.users.begin(), next.users.end(), [&](const Account& account) {
        return account.username == username;
    });
    found->passwordHash = hashPassword(newPassword);
    files_.commit(std::move(next));
}

std::vector<Account> User::listUsers() const {
    requirePermission(Permission::ManageUsers);
    auto users = files_.data().users;
    for (auto& account : users) {
        account.passwordHash.clear();
    }
    return users;
}

}
