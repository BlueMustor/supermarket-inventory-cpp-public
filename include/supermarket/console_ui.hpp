#pragma once

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace market {

enum class Tone { Muted, Accent, Good, Warning, Bad };

struct MenuOption {
    std::string key;
    std::string label;
    std::string hint;
};

struct TableColumn {
    std::string label;
    std::size_t width;
    bool right = false;
};

class ConsoleUi {
public:
    ConsoleUi(std::istream& input, std::ostream& output);
    ~ConsoleUi();
    ConsoleUi(const ConsoleUi&) = delete;
    ConsoleUi& operator=(const ConsoleUi&) = delete;

    void page(const std::string& title, const std::string& subtitle = "");
    void section(const std::string& title);
    void menu(const std::vector<MenuOption>& options);
    void table(const std::vector<TableColumn>& columns,
               const std::vector<std::vector<std::string>>& rows,
               const std::string& emptyMessage = "暂无记录");
    void metrics(const std::vector<std::pair<std::string, std::string>>& values);
    void field(const std::string& label, const std::string& value,
               Tone tone = Tone::Muted);
    void message(const std::string& text, Tone tone = Tone::Muted);
    void prompt(const std::string& text);
    void pause();
    void divider();
    bool interactive() const noexcept;
    std::size_t width() const;
    static std::size_t displayWidth(std::string_view text);
    static std::string fit(std::string_view text, std::size_t width, bool right = false);

private:
    struct Terminal;
    std::string styled(std::string_view text, Tone tone, bool bold = false) const;
    void clear();

    std::istream& input_;
    std::ostream& output_;
    std::unique_ptr<Terminal> terminal_;
};

} // namespace market
