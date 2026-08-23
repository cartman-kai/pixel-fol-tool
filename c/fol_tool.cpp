#include "fol_core.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
void print_usage(const char *program)
{
    std::printf("Pixel FOL Tool (shared core CLI)\n\n");
    std::printf("Usage:\n");
    std::printf("  %s unpack <input.fol> [workspace_dir]\n", program);
    std::printf("  %s pack <workspace_dir> [output.fol]\n", program);
}

void cli_logger(int progress, FolLogLevel level, const char *message, void *)
{
    const char *prefix = "[*]";
    if (level == FOL_LOG_WARNING)
    {
        prefix = "[!]";
    }
    else if (level == FOL_LOG_ERROR)
    {
        prefix = "[x]";
    }
    std::printf("%s %03d%% %s\n", prefix, progress, message);
}

#ifdef _WIN32
std::string utf8_from_wide(const wchar_t *text)
{
    if (text == nullptr || *text == L'\0')
    {
        return {};
    }

    int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(length > 0 ? length : 0, '\0');
    if (length > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), length, nullptr, nullptr);
        if (!utf8.empty() && utf8.back() == '\0')
        {
            utf8.pop_back();
        }
    }
    return utf8;
}
#endif
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc < 2)
    {
        print_usage("fol_tool");
        return 1;
    }

    const std::string command = utf8_from_wide(argv[1]);
    if (command == "unpack")
    {
        if (argc < 3)
        {
            print_usage("fol_tool");
            return 1;
        }

        const std::string input = utf8_from_wide(argv[2]);
        std::string output = argc >= 4 ? utf8_from_wide(argv[3]) : input;
        if (argc < 4)
        {
            const std::size_t dot = output.find_last_of('.');
            if (dot != std::string::npos)
            {
                output.resize(dot);
            }
            output += "_project";
        }

        const int result = fol_unpack(input.c_str(), output.c_str(), cli_logger, nullptr);
        if (result != FOL_SUCCESS)
        {
            std::fprintf(stderr, "[x] %s\n", fol_result_message(result));
        }
        return result == FOL_SUCCESS ? 0 : result;
    }

    if (command == "pack")
    {
        if (argc < 3)
        {
            print_usage("fol_tool");
            return 1;
        }

        const std::string input = utf8_from_wide(argv[2]);
        const std::string output = argc >= 4 ? utf8_from_wide(argv[3]) : "output.fol";
        const int result = fol_pack(input.c_str(), output.c_str(), cli_logger, nullptr);
        if (result != FOL_SUCCESS)
        {
            std::fprintf(stderr, "[x] %s\n", fol_result_message(result));
        }
        return result == FOL_SUCCESS ? 0 : result;
    }

    print_usage("fol_tool");
    return 1;
}
#else
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    if (std::strcmp(argv[1], "unpack") == 0)
    {
        if (argc < 3)
        {
            print_usage(argv[0]);
            return 1;
        }

        std::string output = argc >= 4 ? argv[3] : argv[2];
        if (argc < 4)
        {
            const std::size_t dot = output.find_last_of('.');
            if (dot != std::string::npos)
            {
                output.resize(dot);
            }
            output += "_project";
        }

        const int result = fol_unpack(argv[2], output.c_str(), cli_logger, nullptr);
        if (result != FOL_SUCCESS)
        {
            std::fprintf(stderr, "[x] %s\n", fol_result_message(result));
        }
        return result == FOL_SUCCESS ? 0 : result;
    }

    if (std::strcmp(argv[1], "pack") == 0)
    {
        if (argc < 3)
        {
            print_usage(argv[0]);
            return 1;
        }

        const char *output = argc >= 4 ? argv[3] : "output.fol";
        const int result = fol_pack(argv[2], output, cli_logger, nullptr);
        if (result != FOL_SUCCESS)
        {
            std::fprintf(stderr, "[x] %s\n", fol_result_message(result));
        }
        return result == FOL_SUCCESS ? 0 : result;
    }

    print_usage(argv[0]);
    return 1;
}
#endif
