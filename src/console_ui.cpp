#include "supermarket/console_ui.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace market {
namespace {

struct Glyph {
    std::string text;
    std::size_t columns;
    bool regional = false;
};

std::string encode(std::uint32_t codepoint) {
    std::string result;
    if (codepoint < 0x80U) {
        result.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800U) {
        result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint < 0x10000U) {
        result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        result.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    return result;
}

std::uint32_t decode(std::string_view text, std::size_t& position) {
    const auto first = static_cast<unsigned char>(text[position]);
    if (first < 0x80U) {
        ++position;
        return first;
    }
    std::size_t count = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
        count = 2; value = first & 0x1FU; minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        count = 3; value = first & 0x0FU; minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        count = 4; value = first & 0x07U; minimum = 0x10000U;
    }
    if (count == 0 || count > text.size() - position) {
        ++position;
        return 0xFFFDU;
    }
    for (std::size_t index = 1; index < count; ++index) {
        const auto next = static_cast<unsigned char>(text[position + index]);
        if ((next & 0xC0U) != 0x80U) {
            ++position;
            return 0xFFFDU;
        }
        value = (value << 6U) | (next & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFFU || (value >= 0xD800U && value <= 0xDFFFU)) {
        ++position;
        return 0xFFFDU;
    }
    position += count;
    return value;
}

bool combining(std::uint32_t value) {
    return (value >= 0x0300U && value <= 0x036FU) ||
           (value >= 0x0483U && value <= 0x0489U) ||
           (value >= 0x0591U && value <= 0x05BDU) || value == 0x05BFU ||
           (value >= 0x05C1U && value <= 0x05C2U) ||
           (value >= 0x0610U && value <= 0x061AU) ||
           (value >= 0x064BU && value <= 0x065FU) || value == 0x0670U ||
           (value >= 0x06D6U && value <= 0x06EDU) ||
           (value >= 0x1AB0U && value <= 0x1AFFU) ||
           (value >= 0x1DC0U && value <= 0x1DFFU) ||
           (value >= 0x200BU && value <= 0x200FU) ||
           (value >= 0x202AU && value <= 0x202EU) ||
           (value >= 0x2060U && value <= 0x206FU) ||
           (value >= 0x20D0U && value <= 0x20FFU) ||
           (value >= 0xFE00U && value <= 0xFE0FU) ||
           (value >= 0xFE20U && value <= 0xFE2FU) ||
           (value >= 0x1F3FBU && value <= 0x1F3FFU) ||
           (value >= 0xE0100U && value <= 0xE01EFU);
}

std::size_t codepointWidth(std::uint32_t value) {
    if (combining(value)) return 0;
    if (value >= 0x1100U &&
        (value <= 0x115FU || value == 0x2329U || value == 0x232AU ||
         (value >= 0x231AU && value <= 0x231BU) ||
         (value >= 0x23E9U && value <= 0x23ECU) || value == 0x23F0U || value == 0x23F3U ||
         (value >= 0x25FDU && value <= 0x25FEU) ||
         (value >= 0x2614U && value <= 0x2615U) ||
         (value >= 0x2648U && value <= 0x2653U) || value == 0x267FU || value == 0x2693U ||
         value == 0x26A1U || (value >= 0x26AAU && value <= 0x26ABU) ||
         (value >= 0x26BDU && value <= 0x26BEU) ||
         (value >= 0x26C4U && value <= 0x26C5U) || value == 0x26CEU || value == 0x26D4U ||
         value == 0x26EAU || (value >= 0x26F2U && value <= 0x26F3U) || value == 0x26F5U ||
         value == 0x26FAU || value == 0x26FDU || value == 0x2705U ||
         (value >= 0x270AU && value <= 0x270BU) || value == 0x2728U || value == 0x274CU ||
         value == 0x274EU || (value >= 0x2753U && value <= 0x2755U) || value == 0x2757U ||
         (value >= 0x2795U && value <= 0x2797U) || value == 0x27B0U || value == 0x27BFU ||
         (value >= 0x2B1BU && value <= 0x2B1CU) || value == 0x2B50U || value == 0x2B55U ||
         (value >= 0x2E80U && value <= 0xA4CFU && value != 0x303FU) ||
         (value >= 0xAC00U && value <= 0xD7A3U) ||
         (value >= 0xF900U && value <= 0xFAFFU) ||
         (value >= 0xFE10U && value <= 0xFE19U) ||
         (value >= 0xFE30U && value <= 0xFE6FU) ||
         (value >= 0xFF01U && value <= 0xFF60U) ||
         (value >= 0xFFE0U && value <= 0xFFE6U) ||
         (value >= 0x1F000U && value <= 0x1FAFFU) ||
         (value >= 0x20000U && value <= 0x3FFFDU))) return 2;
    return 1;
}

std::vector<Glyph> glyphs(std::string_view text) {
    std::vector<Glyph> result;
    bool joinNext = false;
    for (std::size_t position = 0; position < text.size();) {
        auto value = decode(text, position);
        if (value < 0x20U || (value >= 0x7FU && value <= 0x9FU)) value = 0x20U;
        const auto columns = codepointWidth(value);
        const bool regional = value >= 0x1F1E6U && value <= 0x1F1FFU;
        if (!result.empty() && (columns == 0 || joinNext || (regional && result.back().regional))) {
            auto& previous = result.back();
            previous.text += encode(value);
            if (joinNext) previous.columns = std::max(previous.columns, columns);
            if (value == 0xFE0FU || value == 0x20E3U) previous.columns = std::max(previous.columns, std::size_t{2});
            if (regional) previous.regional = false;
        } else {
            result.push_back({encode(value), columns, regional});
        }
        joinNext = value == 0x200DU;
    }
    return result;
}

std::string repeat(std::string_view text, std::size_t count) {
    std::string result;
    result.reserve(text.size() * count);
    for (std::size_t index = 0; index < count; ++index) result += text;
    return result;
}

std::vector<std::string> wrap(std::string_view text, std::size_t columns) {
    columns = std::max(columns, std::size_t{1});
    std::vector<std::string> result;
    std::size_t start = 0;
    do {
        const auto end = text.find('\n', start);
        const auto part = text.substr(start, end == std::string_view::npos ? end : end - start);
        std::string line;
        std::size_t used = 0;
        for (const auto& glyph : glyphs(part)) {
            if (used > 0 && used + glyph.columns > columns) {
                result.push_back(std::move(line));
                line.clear();
                used = 0;
            }
            if (glyph.columns > columns) {
                line += "?";
                ++used;
            } else {
                line += glyph.text;
                used += glyph.columns;
            }
        }
        result.push_back(std::move(line));
        if (end == std::string_view::npos) break;
        start = end + 1;
    } while (start <= text.size());
    return result;
}

bool colorsAllowed() {
    if (std::getenv("NO_COLOR") != nullptr) return false;
    const auto* term = std::getenv("TERM");
    return term == nullptr || std::string_view(term) != "dumb";
}

} // namespace

struct ConsoleUi::Terminal {
    bool interactive = false;
    bool ansi = false;
#ifdef _WIN32
    HANDLE output = INVALID_HANDLE_VALUE;
    DWORD originalMode = 0;
    bool modeChanged = false;
    std::wstring title;
    bool titleChanged = false;
#endif
};

ConsoleUi::ConsoleUi(std::istream& input, std::ostream& output)
    : input_(input), output_(output), terminal_(std::make_unique<Terminal>()) {
    if (&input_ != &std::cin || &output_ != &std::cout) return;
#ifdef _WIN32
    DWORD inputMode = 0;
    terminal_->output = GetStdHandle(STD_OUTPUT_HANDLE);
    terminal_->interactive = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &inputMode) != 0 &&
                             GetConsoleMode(terminal_->output, &terminal_->originalMode) != 0;
    if (!terminal_->interactive) return;
    if (colorsAllowed()) {
        terminal_->ansi = SetConsoleMode(terminal_->output,
            terminal_->originalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        terminal_->modeChanged = terminal_->ansi;
    }
    std::wstring title(32768, L'\0');
    const DWORD length = GetConsoleTitleW(title.data(), static_cast<DWORD>(title.size()));
    title.resize(length);
    terminal_->title = std::move(title);
    terminal_->titleChanged = SetConsoleTitleW(L"SUPERMARKET | 小型超市进销存管理系统") != 0;
#else
    terminal_->interactive = isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
    terminal_->ansi = terminal_->interactive && colorsAllowed();
#endif
}

ConsoleUi::~ConsoleUi() {
    if (terminal_->ansi) {
        try { output_ << "\x1b[0m" << std::flush; } catch (...) {}
    }
#ifdef _WIN32
    if (terminal_->modeChanged) SetConsoleMode(terminal_->output, terminal_->originalMode);
    if (terminal_->titleChanged) SetConsoleTitleW(terminal_->title.c_str());
#endif
}

bool ConsoleUi::interactive() const noexcept { return terminal_->interactive; }

std::size_t ConsoleUi::width() const {
    std::size_t columns = 104;
    if (terminal_->interactive) {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO information{};
        if (GetConsoleScreenBufferInfo(terminal_->output, &information)) {
            const auto actual = information.srWindow.Right - information.srWindow.Left + 1;
            if (actual > 0) columns = static_cast<std::size_t>(actual);
        }
#else
        winsize information{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &information) == 0 && information.ws_col > 0) {
            columns = information.ws_col;
        }
#endif
    }
    return std::max(std::size_t{1}, std::min(columns, std::size_t{112}));
}

std::size_t ConsoleUi::displayWidth(std::string_view text) {
    std::size_t columns = 0;
    for (const auto& glyph : glyphs(text)) columns += glyph.columns;
    return columns;
}

std::string ConsoleUi::fit(std::string_view text, std::size_t columns, bool right) {
    const auto parts = glyphs(text);
    std::size_t total = 0;
    for (const auto& part : parts) total += part.columns;
    const bool truncated = total > columns;
    const auto limit = truncated && columns > 0 ? columns - 1 : columns;
    std::string result;
    std::size_t used = 0;
    for (const auto& part : parts) {
        if (used + part.columns > limit) break;
        result += part.text;
        used += part.columns;
    }
    if (truncated && columns > 0) { result += "…"; ++used; }
    if (right) result.insert(0, columns - used, ' ');
    else result.append(columns - used, ' ');
    return result;
}

std::string ConsoleUi::styled(std::string_view text, Tone tone, bool bold) const {
    if (!terminal_->ansi) return std::string(text);
    const char* color = "\x1b[38;5;245m";
    switch (tone) {
    case Tone::Muted: break;
    case Tone::Accent: color = "\x1b[38;5;81m"; break;
    case Tone::Good: color = "\x1b[38;5;114m"; break;
    case Tone::Warning: color = "\x1b[38;5;221m"; break;
    case Tone::Bad: color = "\x1b[38;5;203m"; break;
    }
    return std::string(bold ? "\x1b[1m" : "") + color + std::string(text) + "\x1b[0m";
}

void ConsoleUi::clear() {
    if (!terminal_->interactive) return;
    output_.flush();
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO information{};
    if (GetConsoleScreenBufferInfo(terminal_->output, &information)) {
        const COORD origin{0, information.srWindow.Top};
        const DWORD cells = static_cast<DWORD>(information.dwSize.X) *
                            static_cast<DWORD>(information.srWindow.Bottom - information.srWindow.Top + 1);
        DWORD written = 0;
        FillConsoleOutputCharacterW(terminal_->output, L' ', cells, origin, &written);
        FillConsoleOutputAttribute(terminal_->output, information.wAttributes, cells, origin, &written);
        SetConsoleCursorPosition(terminal_->output, origin);
    }
#else
    if (terminal_->ansi) output_ << "\x1b[2J\x1b[H";
#endif
}

void ConsoleUi::page(const std::string& title, const std::string& subtitle) {
    clear();
    const auto columns = width();
    output_ << '\n' << styled(fit("SUPERMARKET  /  小型超市进销存管理系统", columns), Tone::Accent, true) << '\n';
    divider();
    for (const auto& line : wrap(title, columns)) output_ << styled(line, Tone::Accent, true) << '\n';
    if (!subtitle.empty()) {
        for (const auto& line : wrap(subtitle, columns)) output_ << styled(line, Tone::Muted) << '\n';
    }
    output_ << '\n';
}

void ConsoleUi::divider() { output_ << styled(repeat("─", width()), Tone::Muted) << '\n'; }

void ConsoleUi::section(const std::string& title) {
    output_ << '\n';
    for (const auto& line : wrap(title, width())) output_ << styled(line, Tone::Accent, true) << '\n';
    output_ << '\n';
}

void ConsoleUi::menu(const std::vector<MenuOption>& options) {
    const auto columns = width();
    bool showHints = true;
    if (terminal_->interactive) {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO information{};
        if (GetConsoleScreenBufferInfo(terminal_->output, &information)) {
            showHints = information.srWindow.Bottom - information.srWindow.Top + 1 >= 30;
        }
#else
        winsize information{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &information) == 0 && information.ws_row > 0) {
            showHints = information.ws_row >= 30;
        }
#endif
    }
    const std::size_t indent = columns >= 12 ? 2 : 0;
    const auto available = columns - indent;
    const bool twoColumns = columns >= 80;
    const std::size_t gap = twoColumns ? 3 : 0;
    const auto cell = twoColumns ? (available - gap) / 2 : available;
    const auto itemText = [&](const MenuOption& item, bool hint) {
        if (hint) {
            const auto offset = std::min(cell, std::size_t{5});
            return std::string(offset, ' ') + styled(fit(item.hint, cell - offset), Tone::Muted);
        }
        const auto keyWidth = std::min(cell, std::size_t{5});
        return styled(fit("[" + item.key + "]", keyWidth), Tone::Accent, true) +
               fit(item.label, cell - keyWidth);
    };
    for (std::size_t index = 0; index < options.size(); index += twoColumns ? 2 : 1) {
        output_ << std::string(indent, ' ') << itemText(options[index], false);
        if (twoColumns && index + 1 < options.size()) {
            output_ << std::string(gap, ' ') << itemText(options[index + 1], false);
        }
        output_ << '\n';
        if (showHints) {
            output_ << std::string(indent, ' ') << itemText(options[index], true);
            if (twoColumns && index + 1 < options.size()) {
                output_ << std::string(gap, ' ') << itemText(options[index + 1], true);
            }
            output_ << '\n';
        }
    }
    output_ << '\n';
}

void ConsoleUi::table(const std::vector<TableColumn>& columns,
                      const std::vector<std::vector<std::string>>& rows,
                      const std::string& emptyMessage) {
    if (rows.empty()) { message(emptyMessage); return; }
    if (columns.empty()) return;
    const auto available = width();
    const auto statusTone = [](std::string_view value) {
        if (value == "预警") return Tone::Warning;
        if (value == "在售" || value == "启用") return Tone::Good;
        return Tone::Muted;
    };
    std::vector<std::size_t> widths;
    widths.reserve(columns.size());
    for (std::size_t index = 0; index < columns.size(); ++index) {
        std::size_t longest = displayWidth(columns[index].label);
        for (const auto& row : rows) {
            if (index < row.size()) longest = std::max(longest, displayWidth(row[index]));
        }
        const auto requested = columns[index].width == 0 ? longest : columns[index].width;
        widths.push_back(columns[index].right ? std::max(requested, longest) :
                         std::max(requested, displayWidth(columns[index].label)));
    }
    const auto totalWidth = [&]() {
        std::size_t total = 1 + 3 * widths.size();
        for (const auto value : widths) total += value;
        return total;
    };
    while (totalWidth() > available) {
        std::size_t candidate = columns.size();
        std::size_t mostSpace = 0;
        for (std::size_t index = 0; index < columns.size(); ++index) {
            if (columns[index].right) continue;
            const auto minimum = std::max(std::size_t{6}, displayWidth(columns[index].label));
            if (widths[index] > minimum && widths[index] - minimum > mostSpace) {
                candidate = index;
                mostSpace = widths[index] - minimum;
            }
        }
        if (candidate == columns.size()) break;
        --widths[candidate];
    }
    if (totalWidth() > available) {
        for (std::size_t index = 0; index < rows.size(); ++index) {
            section("记录 " + std::to_string(index + 1));
            for (std::size_t column = 0; column < columns.size(); ++column) {
                const auto value = column < rows[index].size() ? rows[index][column] : "";
                field(columns[column].label, value, columns[column].label == "状态" ? statusTone(value) : Tone::Muted);
            }
        }
        return;
    }
    const auto rule = [&](std::string_view left, std::string_view middle, std::string_view right) {
        std::string line(left);
        for (std::size_t index = 0; index < widths.size(); ++index) {
            line += repeat("─", widths[index] + 2);
            line += index + 1 < widths.size() ? middle : right;
        }
        output_ << styled(line, Tone::Muted) << '\n';
    };
    rule("┌", "┬", "┐");
    output_ << styled("│", Tone::Muted);
    for (std::size_t index = 0; index < columns.size(); ++index) {
        output_ << ' ' << styled(fit(columns[index].label, widths[index], columns[index].right), Tone::Accent, true)
                << ' ' << styled("│", Tone::Muted);
    }
    output_ << '\n';
    rule("├", "┼", "┤");
    for (const auto& row : rows) {
        output_ << styled("│", Tone::Muted);
        for (std::size_t index = 0; index < columns.size(); ++index) {
            const std::string_view value = index < row.size() ? std::string_view(row[index]) : std::string_view{};
            const auto cell = fit(value, widths[index], columns[index].right);
            const bool status = columns[index].label == "状态" &&
                                (value == "预警" || value == "停用" || value == "在售" || value == "启用");
            output_ << ' ' << (status ? styled(cell, statusTone(value)) : cell)
                    << ' ' << styled("│", Tone::Muted);
        }
        output_ << '\n';
    }
    rule("└", "┴", "┘");
}

void ConsoleUi::metrics(const std::vector<std::pair<std::string, std::string>>& values) {
    if (values.empty()) return;
    const auto available = width();
    if (available < 20) {
        for (const auto& value : values) field(value.first, value.second, Tone::Accent);
        return;
    }
    std::size_t longestLabel = 0;
    for (const auto& value : values) longestLabel = std::max(longestLabel, displayWidth(value.first));
    const auto minimumCard = std::clamp(longestLabel + 4, std::size_t{18}, std::size_t{26});
    const auto perRow = std::min(values.size(), std::max(std::size_t{1}, (available + 2) / (minimumCard + 2)));
    const auto cardWidth = (available - (perRow - 1) * 2) / perRow;
    for (std::size_t start = 0; start < values.size(); start += perRow) {
        const auto end = std::min(values.size(), start + perRow);
        for (std::size_t index = start; index < end; ++index) {
            if (index > start) output_ << "  ";
            output_ << styled("┌" + repeat("─", cardWidth - 2) + "┐", Tone::Muted);
        }
        output_ << '\n';
        for (std::size_t index = start; index < end; ++index) {
            if (index > start) output_ << "  ";
            output_ << styled("│", Tone::Muted) << ' ' << styled(fit(values[index].first, cardWidth - 4), Tone::Muted)
                    << ' ' << styled("│", Tone::Muted);
        }
        output_ << '\n';
        std::vector<std::vector<std::string>> lines;
        std::size_t height = 1;
        for (std::size_t index = start; index < end; ++index) {
            lines.push_back(wrap(values[index].second, cardWidth - 4));
            height = std::max(height, lines.back().size());
        }
        for (std::size_t line = 0; line < height; ++line) {
            for (std::size_t index = start; index < end; ++index) {
                if (index > start) output_ << "  ";
                const auto& card = lines[index - start];
                output_ << styled("│", Tone::Muted) << ' '
                        << styled(fit(line < card.size() ? card[line] : "", cardWidth - 4), Tone::Accent, true)
                        << ' ' << styled("│", Tone::Muted);
            }
            output_ << '\n';
        }
        for (std::size_t index = start; index < end; ++index) {
            if (index > start) output_ << "  ";
            output_ << styled("└" + repeat("─", cardWidth - 2) + "┘", Tone::Muted);
        }
        output_ << '\n';
    }
    output_ << '\n';
}

void ConsoleUi::field(const std::string& label, const std::string& value, Tone tone) {
    const auto available = width();
    const auto labelWidth = displayWidth(label);
    if (available < 16 || labelWidth + 8 >= available) {
        for (const auto& line : wrap(label + "：", available)) output_ << styled(line, Tone::Muted) << '\n';
        for (const auto& line : wrap(value, available)) output_ << styled(line, tone) << '\n';
        return;
    }
    const auto prefixWidth = labelWidth + 4;
    const auto lines = wrap(value, available - prefixWidth);
    output_ << styled("  " + label + "  ", Tone::Muted) << styled(lines.front(), tone) << '\n';
    for (std::size_t index = 1; index < lines.size(); ++index) {
        output_ << std::string(prefixWidth, ' ') << styled(lines[index], tone) << '\n';
    }
}

void ConsoleUi::message(const std::string& text, Tone tone) {
    const auto available = width();
    std::string prefix = "· ";
    if (tone == Tone::Good) prefix = "✓ ";
    else if (tone == Tone::Warning) prefix = "! ";
    else if (tone == Tone::Bad) prefix = "× ";
    else if (tone == Tone::Accent) prefix = "› ";
    if (available < 4) prefix.clear();
    const auto offset = displayWidth(prefix);
    const auto lines = wrap(text, available - offset);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        output_ << styled(index == 0 ? prefix : std::string(offset, ' '), tone)
                << styled(lines[index], tone) << '\n';
    }
}

void ConsoleUi::prompt(const std::string& text) {
    output_ << '\n';
    const auto available = width();
    const auto prefix = available >= 4 ? std::string("› ") : std::string{};
    const auto lines = wrap(text, available - displayWidth(prefix));
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) output_ << '\n';
        output_ << styled(prefix + lines[index], Tone::Accent);
    }
    output_ << std::flush;
}

void ConsoleUi::pause() {
    if (!terminal_->interactive || !input_) return;
    prompt("按 Enter 返回 ");
    std::string ignored;
    std::getline(input_, ignored);
}

} // namespace market
