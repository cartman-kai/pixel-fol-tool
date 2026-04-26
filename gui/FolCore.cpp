#define NOMINMAX
#include "FolCore.hpp"

#include <windows.h>
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

    logger->callback(progress, wide_from_utf8(message));
}

void log_result_if_failed(int result, const LogCallback& logger)
{
    if (result != FOL_SUCCESS && logger)
    {
        logger(0, L"Error: " + wide_from_utf8(fol_result_message(result)));
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
