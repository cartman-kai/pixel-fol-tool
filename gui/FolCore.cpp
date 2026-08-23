#define NOMINMAX
#include "FolCore.hpp"

#include <windows.h>
#include <cwchar>
#include <string>

namespace
{
std::string narrow_from_wide(const std::wstring& text, unsigned int codePage)
{
    if (text.empty())
    {
        return {};
    }

    int length = WideCharToMultiByte(codePage, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(length > 0 ? length : 0, '\0');
    if (length > 0)
    {
        WideCharToMultiByte(codePage, 0, text.c_str(), -1, out.data(), length, nullptr, nullptr);
        if (!out.empty() && out.back() == '\0')
        {
            out.pop_back();
        }
    }
    return out;
}

std::wstring wide_from_narrow(const char* text, unsigned int codePage)
{
    if (text == nullptr || *text == '\0')
    {
        return {};
    }

    int length = MultiByteToWideChar(codePage, 0, text, -1, nullptr, 0);
    std::wstring wide(length > 0 ? length : 0, L'\0');
    if (length > 0)
    {
        MultiByteToWideChar(codePage, 0, text, -1, wide.data(), length);
        if (!wide.empty() && wide.back() == L'\0')
        {
            wide.pop_back();
        }
    }
    return wide;
}

std::string utf8_from_wide(const std::wstring& text)
{
    return narrow_from_wide(text, CP_UTF8);
}

std::wstring wide_from_utf8(const char* text)
{
    return wide_from_narrow(text, CP_UTF8);
}

bool starts_with(const std::wstring& text, const wchar_t* prefix)
{
    const std::wstring prefix_text(prefix);
    return text.rfind(prefix_text, 0) == 0;
}

std::wstring after_prefix(const std::wstring& text, const wchar_t* prefix)
{
    return text.substr(std::wcslen(prefix));
}

std::wstring translate_core_message(const char* message)
{
    const std::wstring text = wide_from_utf8(message);

    if (starts_with(text, L"Starting unpack: "))
    {
        return L"开始解包：" + after_prefix(text, L"Starting unpack: ");
    }
    if (starts_with(text, L"Detected encrypted archive with "))
    {
        std::wstring rest = after_prefix(text, L"Detected encrypted archive with ");
        const std::wstring suffix = L" entries";
        const std::size_t suffix_pos = rest.rfind(suffix);
        if (suffix_pos != std::wstring::npos)
        {
            rest.resize(suffix_pos);
        }
        return L"检测到加密归档，文件数量：" + rest;
    }
    if (starts_with(text, L"Extracted: "))
    {
        return L"已解包：" + after_prefix(text, L"Extracted: ");
    }
    if (text == L"Unpack completed")
    {
        return L"解包完成。";
    }
    if (starts_with(text, L"Starting pack: "))
    {
        return L"开始打包：" + after_prefix(text, L"Starting pack: ");
    }
    if (starts_with(text, L"Scanned files: "))
    {
        return L"已扫描文件：" + after_prefix(text, L"Scanned files: ");
    }
    if (starts_with(text, L"Packed: "))
    {
        return L"已打包：" + after_prefix(text, L"Packed: ");
    }
    if (starts_with(text, L"Pack completed: "))
    {
        return L"打包完成：" + after_prefix(text, L"Pack completed: ");
    }

    return text;
}

std::wstring translate_result_message(int result)
{
    switch (result)
    {
    case FOL_ERROR_INVALID_ARGUMENT:
        return L"参数无效。";
    case FOL_ERROR_OPEN_INPUT:
        return L"无法打开输入文件。";
    case FOL_ERROR_OPEN_OUTPUT:
        return L"无法打开输出文件。";
    case FOL_ERROR_READ:
        return L"读取文件内容失败。";
    case FOL_ERROR_WRITE:
        return L"写入文件内容失败。";
    case FOL_ERROR_FORMAT:
        return L"FOL 格式无效或暂不支持。";
    case FOL_ERROR_MANIFEST:
        return L"工作区目录无效。";
    case FOL_ERROR_FILESYSTEM:
        return L"文件系统操作失败。";
    default:
        return L"未知错误。";
    }
}

struct GuiLogger
{
    LogCallback callback;
};

void core_logger(int progress, FolLogLevel, const char* message, void* user_data)
{
    auto* logger = static_cast<GuiLogger*>(user_data);
    if (logger == nullptr || !logger->callback)
    {
        return;
    }

    logger->callback(progress, translate_core_message(message));
}

void log_result_if_failed(int result, const LogCallback& logger)
{
    if (result != FOL_SUCCESS && logger)
    {
        logger(0, L"错误：" + translate_result_message(result));
    }
}
}

int FolCore::Unpack(const std::wstring& inputFol, const std::wstring& outputDir, LogCallback logger)
{
    const std::string input_utf8 = utf8_from_wide(inputFol);
    const std::string output_utf8 = utf8_from_wide(outputDir);
    GuiLogger state{ logger };
    const int result = fol_unpack(input_utf8.c_str(), output_utf8.c_str(), core_logger, &state);
    log_result_if_failed(result, logger);
    return result;
}

int FolCore::Pack(const std::wstring& inputDir, const std::wstring& outputFol, LogCallback logger)
{
    const std::string input_utf8 = utf8_from_wide(inputDir);
    const std::string output_utf8 = utf8_from_wide(outputFol);
    GuiLogger state{ logger };
    const int result = fol_pack(input_utf8.c_str(), output_utf8.c_str(), core_logger, &state);
    log_result_if_failed(result, logger);
    return result;
}
