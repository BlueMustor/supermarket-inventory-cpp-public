#include "supermarket/models.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace market {
namespace {

bool isDigit(char value) {
    return value >= '0' && value <= '9';
}

bool isValidUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            ++i;
            continue;
        }
        const std::size_t length = first >= 0xC2 && first <= 0xDF ? 2 :
                                   first >= 0xE0 && first <= 0xEF ? 3 :
                                   first >= 0xF0 && first <= 0xF4 ? 4 : 0;
        if (length == 0 || i + length > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < length; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if (next < 0x80 || next > 0xBF) {
                return false;
            }
        }
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second >= 0xA0) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second >= 0x90)) {
            return false;
        }
        i += length;
    }
    return true;
}

}

std::int64_t parseInteger(std::string_view text, std::int64_t minimum, std::int64_t maximum) {
    if (text.empty() || minimum > maximum) {
        throw Error("请输入有效整数。");
    }
    std::int64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value < minimum || value > maximum) {
        throw Error("整数格式不正确或超出允许范围。");
    }
    return value;
}

Money parseMoney(std::string_view text) {
    if (text.empty() || text.size() > 16) {
        throw Error("金额不能为空或超出允许范围。");
    }
    const auto dot = text.find('.');
    const auto whole = text.substr(0, dot);
    if (whole.empty() || !std::all_of(whole.begin(), whole.end(), isDigit)) {
        throw Error("金额格式应为 12、12.3 或 12.34，不接受符号或科学计数法。");
    }
    const Money yuan = parseInteger(whole, 0, maxMoney / 100);
    Money cents = 0;
    if (dot != std::string_view::npos) {
        const auto fraction = text.substr(dot + 1);
        if (fraction.empty() || fraction.size() > 2 ||
            !std::all_of(fraction.begin(), fraction.end(), isDigit)) {
            throw Error("金额的小数部分应为一位或两位。");
        }
        cents = parseInteger(fraction, 0, 99);
        if (fraction.size() == 1) {
            cents *= 10;
        }
    }
    return checkedAdd(yuan * 100, cents);
}

std::string formatMoney(Money value) {
    const bool negative = value < 0;
    const auto absolute = negative ? static_cast<std::uint64_t>(-(value + 1)) + 1 :
                                     static_cast<std::uint64_t>(value);
    std::ostringstream output;
    if (negative) {
        output << '-';
    }
    output << absolute / 100 << '.' << std::setw(2) << std::setfill('0') << absolute % 100;
    return output.str();
}

Money checkedAdd(Money left, Money right) {
    if (left < 0 || right < 0 || left > maxMoney || right > maxMoney - left) {
        throw Error("合计超出系统支持的金额或数量范围。");
    }
    return left + right;
}

Money checkedMultiply(Money price, Quantity quantity) {
    if (price < 0 || price > maxMoney || quantity < 0 || quantity > maxQuantity ||
        (quantity != 0 && price > maxMoney / quantity)) {
        throw Error("单价或数量不合法，或乘积超出系统支持的金额范围。");
    }
    return price * quantity;
}

void requireText(std::string_view text, std::string_view label, std::size_t maximum) {
    const bool blank = std::all_of(text.begin(), text.end(), [](unsigned char ch) { return ch == ' '; });
    const bool control = std::any_of(text.begin(), text.end(), [](unsigned char ch) {
        return ch < 32 || ch == 127;
    });
    if (text.empty() || text.size() > maximum || blank || control || !isValidUtf8(text)) {
        throw Error(std::string(label) + "不能为空、仅含空格、包含控制字符、编码无效或超过字节长度限制。");
    }
}

void validateDate(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        throw Error("日期格式必须为 YYYY-MM-DD。");
    }
    const int year = static_cast<int>(parseInteger(date.substr(0, 4), 1900, 9999));
    const int month = static_cast<int>(parseInteger(date.substr(5, 2), 1, 12));
    int maximumDay = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        maximumDay = 30;
    } else if (month == 2) {
        const bool leap = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
        maximumDay = leap ? 29 : 28;
    }
    parseInteger(date.substr(8, 2), 1, maximumDay);
    for (std::size_t index = 0; index < date.size(); ++index) {
        if (index != 4 && index != 7 && !isDigit(date[index])) {
            throw Error("日期中只能包含数字和分隔符。");
        }
    }
}

void validateDateRange(std::string_view start, std::string_view end) {
    if (!start.empty()) {
        validateDate(start);
    }
    if (!end.empty()) {
        validateDate(end);
    }
    if (!start.empty() && !end.empty() && start > end) {
        throw Error("开始日期不能晚于结束日期。");
    }
}

bool inDateRange(std::string_view timestamp, std::string_view start, std::string_view end) {
    const auto date = timestamp.substr(0, 10);
    return (start.empty() || date >= start) && (end.empty() || date <= end);
}

std::string nowTimestamp() {
    const auto now = std::time(nullptr);
    std::tm calendar{};
#ifdef _WIN32
    if (localtime_s(&calendar, &now) != 0) {
        throw Error("无法读取当前本地时间。");
    }
#else
    if (localtime_r(&now, &calendar) == nullptr) {
        throw Error("无法读取当前本地时间。");
    }
#endif
    std::ostringstream output;
    output << std::put_time(&calendar, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string makeId(char prefix, std::uint64_t& counter) {
    if (counter == 0 || counter == std::numeric_limits<std::uint64_t>::max()) {
        throw Error("编号已超出允许范围。");
    }
    std::ostringstream output;
    output << prefix << std::setw(6) << std::setfill('0') << counter;
    ++counter;
    return output.str();
}

std::string roleName(Role role) {
    switch (role) {
    case Role::Admin: return "管理员";
    case Role::Cashier: return "收银员";
    }
    throw Error("未知用户角色。");
}

}
