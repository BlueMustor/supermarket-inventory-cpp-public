#include "supermarket/services.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

#ifdef _WIN32
class ConsoleEncoding {
public:
    ConsoleEncoding() : inputCodePage_(GetConsoleCP()), outputCodePage_(GetConsoleOutputCP()) {
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }

    ~ConsoleEncoding() {
        if (inputCodePage_ != 0) SetConsoleCP(inputCodePage_);
        if (outputCodePage_ != 0) SetConsoleOutputCP(outputCodePage_);
    }

private:
    UINT inputCodePage_;
    UINT outputCodePage_;
};

std::filesystem::path executablePath() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (count == 0) throw market::Error("无法确定程序所在目录。");
        if (count < buffer.size()) return std::filesystem::path(std::wstring(buffer.data(), count));
        if (buffer.size() >= 32768) throw market::Error("程序路径过长。");
        buffer.resize(buffer.size() * 2);
    }
}

std::vector<std::filesystem::path> arguments(int, char**) {
    int count = 0;
    wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) throw market::Error("无法读取启动参数。");
    struct ArgumentOwner {
        wchar_t** values;
        ~ArgumentOwner() { LocalFree(values); }
    } owner{values};
    std::vector<std::filesystem::path> result;
    for (int index = 1; index < count; ++index) result.emplace_back(values[index]);
    return result;
}
#else
std::filesystem::path executablePath(const char* program) {
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return path;
    const std::filesystem::path fallback(program);
    if (fallback.has_parent_path()) return std::filesystem::absolute(fallback);
    return std::filesystem::current_path() / fallback;
}

std::vector<std::filesystem::path> arguments(int count, char** values) {
    std::vector<std::filesystem::path> result;
    for (int index = 1; index < count; ++index) result.emplace_back(values[index]);
    return result;
}
#endif

void showHelp() {
    std::cout << "小型超市商品进销存管理系统\n"
                 "用法：supermarket [--data-dir 路径] [--help]\n"
                 "  --data-dir 路径  指定数据目录；默认是可执行文件所在目录下的 data\n"
                 "  --help           显示帮助并退出\n"
                 "首次启动需要创建管理员账号，没有预设用户名和密码。\n";
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    ConsoleEncoding consoleEncoding;
#endif
    try {
        const auto options = arguments(argc, argv);
        std::filesystem::path dataDirectory;
        bool dataDirectorySpecified = false;
        bool help = false;
        for (std::size_t index = 0; index < options.size(); ++index) {
            if (options[index] == "--help") {
                help = true;
            } else if (options[index] == "--data-dir") {
                if (dataDirectorySpecified) throw market::Error("--data-dir 只能指定一次。");
                if (++index == options.size() || options[index].empty() ||
                    options[index].u8string().rfind("--", 0) == 0) {
                    throw market::Error("--data-dir 后必须提供数据目录路径。");
                }
                dataDirectory = options[index];
                dataDirectorySpecified = true;
            } else {
                throw market::Error("未知参数：" + options[index].u8string() + "。使用 --help 查看用法。");
            }
        }
        if (help) {
            showHelp();
            return 0;
        }
        if (!dataDirectorySpecified) {
#ifdef _WIN32
            dataDirectory = executablePath().parent_path() / "data";
#else
            dataDirectory = executablePath(argv[0]).parent_path() / "data";
#endif
        }
        market::FileManager files(std::filesystem::absolute(dataDirectory).lexically_normal());
        market::Menu menu(files, std::cin, std::cout);
        menu.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "程序无法继续：" << error.what() << '\n';
        return 1;
    }
}
