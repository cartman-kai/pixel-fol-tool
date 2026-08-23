#include "fol_core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
constexpr std::size_t kIndexEntrySize = 136;
constexpr std::size_t kPaddingCount = 97;

struct DiskEntry
{
    fs::path path;
    std::string game_path;
    std::uint32_t key = 0;
};

std::mt19937 &rng()
{
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

void log_message(FolLogCallback callback, void *user_data, int progress, FolLogLevel level, const std::string &message)
{
    if (callback != nullptr)
    {
        callback(progress, level, message.c_str(), user_data);
    }
}

#ifdef _WIN32
std::string narrow_from_wide(const std::wstring &wide, unsigned int code_page)
{
    if (wide.empty())
    {
        return std::string();
    }

    int length = WideCharToMultiByte(code_page, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(length > 0 ? length : 0, '\0');
    if (length > 0)
    {
        WideCharToMultiByte(code_page, 0, wide.c_str(), -1, text.data(), length, nullptr, nullptr);
        if (!text.empty() && text.back() == '\0')
        {
            text.pop_back();
        }
    }
    return text;
}

std::wstring wide_from_narrow(const char *text, unsigned int code_page)
{
    if (text == nullptr || *text == '\0')
    {
        return std::wstring();
    }

    int length = MultiByteToWideChar(code_page, 0, text, -1, nullptr, 0);
    std::wstring wide(length > 0 ? length : 0, L'\0');
    if (length > 0)
    {
        MultiByteToWideChar(code_page, 0, text, -1, wide.data(), length);
        if (!wide.empty() && wide.back() == L'\0')
        {
            wide.pop_back();
        }
    }
    return wide;
}
#endif

fs::path path_from_utf8(const char *text)
{
    if (text == nullptr)
    {
        return fs::path();
    }
#ifdef _WIN32
    return fs::path(wide_from_narrow(text, CP_UTF8));
#else
    return fs::path(text);
#endif
}

std::string utf8_from_path(const fs::path &path)
{
#ifdef _WIN32
    return narrow_from_wide(path.native(), CP_UTF8);
#else
    return path.string();
#endif
}

#ifdef _WIN32
std::string gb2312_from_wide(const std::wstring &wide)
{
    return narrow_from_wide(wide, 936);
}

std::wstring wide_from_gb2312(const std::string &text)
{
    return wide_from_narrow(text.c_str(), 936);
}
#endif

fs::path local_path_from_game_path(const std::string &game_path)
{
#ifdef _WIN32
    std::wstring wide = wide_from_gb2312(game_path);
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    return fs::path(wide);
#else
    std::string local = game_path;
    std::replace(local.begin(), local.end(), '\\', '/');
    return fs::path(local);
#endif
}

std::string game_path_from_relative_path(const fs::path &path)
{
#ifdef _WIN32
    std::wstring wide = path.generic_wstring();
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    return gb2312_from_wide(wide);
#else
    std::string text = path.generic_string();
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
#endif
}

std::string log_text_from_game_path(const std::string &game_path)
{
#ifdef _WIN32
    return narrow_from_wide(wide_from_gb2312(game_path), CP_UTF8);
#else
    return game_path;
#endif
}

void transform_content(std::vector<std::uint8_t> &data, std::uint32_t key, bool encrypt)
{
    const std::size_t count = data.size() / sizeof(std::uint32_t);
    for (std::size_t i = 0; i < count; ++i)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, data.data() + i * sizeof(value), sizeof(value));
        std::uint32_t term = 99U * static_cast<std::uint32_t>(i * i);
        value = encrypt ? value + key + term : value - key - term;
        std::memcpy(data.data() + i * sizeof(value), &value, sizeof(value));
    }
}

void transform_index(std::vector<std::uint8_t> &data, std::uint32_t key, bool encrypt)
{
    for (std::size_t i = 0; i < kIndexEntrySize / sizeof(std::uint32_t); ++i)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, data.data() + i * sizeof(value), sizeof(value));
        std::uint32_t term = 9U * static_cast<std::uint32_t>(i * i * i);
        value = encrypt ? value + key + term : value - key - term;
        std::memcpy(data.data() + i * sizeof(value), &value, sizeof(value));
    }
}

std::uint32_t read_u32(const std::uint8_t *data)
{
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

void write_u32(std::uint8_t *data, std::uint32_t value)
{
    std::memcpy(data, &value, sizeof(value));
}

bool read_exact(FILE *file, void *buffer, std::size_t size)
{
    return std::fread(buffer, 1, size, file) == size;
}

bool write_exact(FILE *file, const void *buffer, std::size_t size)
{
    return std::fwrite(buffer, 1, size, file) == size;
}

bool is_ignored_file(const fs::path &path)
{
    const std::string name = path.filename().string();
    return name == ".DS_Store";
}

bool is_windows_reserved_name(const fs::path &component)
{
    std::string name = component.filename().string();
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos)
    {
        name.resize(dot);
    }
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    static const char *reserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    for (const char *reserved_name : reserved)
    {
        if (name == reserved_name)
        {
            return true;
        }
    }
    return false;
}

bool is_safe_relative_path(const fs::path &path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
    {
        return false;
    }

    for (const auto &component : path)
    {
        if (component.empty() || component == "." || component == ".." || is_windows_reserved_name(component))
        {
            return false;
        }
    }
    return true;
}

bool safe_output_path(const fs::path &base_dir, const std::string &game_path, fs::path &output)
{
    if (game_path.empty() || game_path.find('\0') != std::string::npos)
    {
        return false;
    }

    const fs::path relative = local_path_from_game_path(game_path);
    if (!is_safe_relative_path(relative))
    {
        return false;
    }

    output = base_dir / relative;
    return true;
}

std::vector<DiskEntry> scan_workspace_files(const fs::path &root_dir, int &result_code)
{
    std::vector<DiskEntry> files;
    std::error_code error;
    fs::recursive_directory_iterator it(root_dir, error);
    if (error)
    {
        result_code = FOL_ERROR_FILESYSTEM;
        return {};
    }

    for (const auto &entry : it)
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        if (is_ignored_file(entry.path()))
        {
            continue;
        }

        DiskEntry file;
        file.path = entry.path();
        file.game_path = game_path_from_relative_path(fs::relative(entry.path(), root_dir));
        fs::path unused;
        if (!safe_output_path(fs::path("."), file.game_path, unused))
        {
            result_code = FOL_ERROR_FILESYSTEM;
            return {};
        }
        files.push_back(std::move(file));
    }

    result_code = FOL_SUCCESS;
    return files;
}

std::uint32_t random_key()
{
    std::uniform_int_distribution<std::uint32_t> distribution(0, 0xFFFFFFFFu);
    return distribution(rng());
}
} // namespace

int fol_unpack(const char *input_fol, const char *output_dir, FolLogCallback callback, void *user_data)
{
    if (input_fol == nullptr || output_dir == nullptr)
    {
        return FOL_ERROR_INVALID_ARGUMENT;
    }

    const fs::path input_path = path_from_utf8(input_fol);
    const fs::path output_path = path_from_utf8(output_dir);

    log_message(callback, user_data, 0, FOL_LOG_INFO, "Starting unpack: " + utf8_from_path(input_path));

    FILE *input = nullptr;
#ifdef _WIN32
    _wfopen_s(&input, input_path.c_str(), L"rb");
#else
    input = std::fopen(input_path.c_str(), "rb");
#endif
    if (input == nullptr)
    {
        return FOL_ERROR_OPEN_INPUT;
    }

    std::error_code file_size_error;
    const std::uintmax_t archive_size = fs::file_size(input_path, file_size_error);
    if (file_size_error || archive_size < sizeof(std::uint32_t) + kPaddingCount * sizeof(std::uint32_t))
    {
        std::fclose(input);
        return FOL_ERROR_FORMAT;
    }

    int result = FOL_SUCCESS;
    std::int32_t raw_count = 0;
    if (!read_exact(input, &raw_count, sizeof(raw_count)))
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    const bool encrypted = raw_count < 0;
    const int file_count = raw_count & 0x7FFFFFFF;
    if (!encrypted || file_count <= 0)
    {
        std::fclose(input);
        return FOL_ERROR_FORMAT;
    }

    const std::uintmax_t index_size = static_cast<std::uintmax_t>(file_count) * kIndexEntrySize;
    const std::uintmax_t key_table_size = static_cast<std::uintmax_t>(file_count) * sizeof(std::uint32_t);
    const std::uintmax_t footer_size = key_table_size + kPaddingCount * sizeof(std::uint32_t);
    const std::uintmax_t data_base_wide = sizeof(std::uint32_t) + index_size;
    if (index_size > std::numeric_limits<std::size_t>::max() ||
        data_base_wide > std::numeric_limits<std::uint32_t>::max() ||
        footer_size > archive_size ||
        data_base_wide > archive_size - footer_size)
    {
        std::fclose(input);
        return FOL_ERROR_FORMAT;
    }

    std::error_code fs_error;
    fs::create_directories(output_path, fs_error);
    if (fs_error)
    {
        std::fclose(input);
        return FOL_ERROR_FILESYSTEM;
    }

    log_message(callback, user_data, 5, FOL_LOG_INFO, "Detected encrypted archive with " + std::to_string(file_count) + " entries");

    if (std::fseek(input, -static_cast<long>(footer_size), SEEK_END) != 0)
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    std::vector<std::uint32_t> keys(static_cast<std::size_t>(file_count));
    if (!read_exact(input, keys.data(), keys.size() * sizeof(std::uint32_t)))
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    if (std::fseek(input, static_cast<long>(sizeof(std::uint32_t)), SEEK_SET) != 0)
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    std::vector<std::uint8_t> index_data(static_cast<std::size_t>(index_size));
    if (!read_exact(input, index_data.data(), index_data.size()))
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    const std::uint32_t data_base = static_cast<std::uint32_t>(data_base_wide);
    const std::uintmax_t key_table_offset = archive_size - footer_size;

    for (int i = 0; i < file_count; ++i)
    {
        std::vector<std::uint8_t> entry(kIndexEntrySize);
        std::memcpy(entry.data(), index_data.data() + static_cast<std::size_t>(i) * kIndexEntrySize, kIndexEntrySize);

        const std::uint32_t key = keys[static_cast<std::size_t>(i)];
        transform_index(entry, key, false);

        char name_buffer[129] = {};
        std::memcpy(name_buffer, entry.data(), 128);
        std::string game_path(name_buffer);

        std::uint32_t offset = read_u32(entry.data() + 128);
        std::uint32_t size = read_u32(entry.data() + 132);
        if (offset < data_base && offset > 0)
        {
            offset += data_base;
        }

        if (size > 0)
        {
            const std::uintmax_t end_offset = static_cast<std::uintmax_t>(offset) + size;
            if (end_offset < offset || end_offset > key_table_offset)
            {
                result = FOL_ERROR_FORMAT;
                break;
            }

            if (std::fseek(input, static_cast<long>(offset), SEEK_SET) != 0)
            {
                result = FOL_ERROR_READ;
                break;
            }

            std::vector<std::uint8_t> content(size);
            if (!read_exact(input, content.data(), content.size()))
            {
                result = FOL_ERROR_READ;
                break;
            }

            transform_content(content, key, false);
            fs::path output_file;
            if (!safe_output_path(output_path, game_path, output_file))
            {
                result = FOL_ERROR_FORMAT;
                break;
            }
            fs::create_directories(output_file.parent_path(), fs_error);
            if (fs_error)
            {
                result = FOL_ERROR_FILESYSTEM;
                break;
            }

            std::ofstream output(output_file, std::ios::binary);
            if (!output)
            {
                result = FOL_ERROR_OPEN_OUTPUT;
                break;
            }
            output.write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
            if (!output.good())
            {
                result = FOL_ERROR_WRITE;
                break;
            }
        }

        const int progress = 10 + static_cast<int>((static_cast<double>(i + 1) / file_count) * 85.0);
        log_message(callback, user_data, progress, FOL_LOG_INFO, "Extracted: " + log_text_from_game_path(game_path));
    }

    std::fclose(input);
    if (result != FOL_SUCCESS)
    {
        return result;
    }

    log_message(callback, user_data, 100, FOL_LOG_INFO, "Unpack completed");
    return FOL_SUCCESS;
}

int fol_pack(const char *input_dir, const char *output_fol, FolLogCallback callback, void *user_data)
{
    if (input_dir == nullptr || output_fol == nullptr)
    {
        return FOL_ERROR_INVALID_ARGUMENT;
    }

    const fs::path workspace_dir = path_from_utf8(input_dir);
    const fs::path output_path = path_from_utf8(output_fol);

    if (!fs::is_directory(workspace_dir))
    {
        return FOL_ERROR_MANIFEST;
    }

    log_message(callback, user_data, 0, FOL_LOG_INFO, "Starting pack: " + utf8_from_path(workspace_dir));

    int result = FOL_SUCCESS;
    std::vector<DiskEntry> final_entries = scan_workspace_files(workspace_dir, result);
    if (result != FOL_SUCCESS)
    {
        return result;
    }
    log_message(callback, user_data, 20, FOL_LOG_INFO, "Scanned files: " + std::to_string(final_entries.size()));

    for (auto &entry : final_entries)
    {
        entry.key = random_key();
    }
    std::sort(final_entries.begin(), final_entries.end(), [](const DiskEntry &lhs, const DiskEntry &rhs) {
        return lhs.game_path < rhs.game_path;
    });

    if (final_entries.empty())
    {
        return FOL_ERROR_MANIFEST;
    }
    if (final_entries.size() > 0x7FFFFFFFu ||
        final_entries.size() > (std::numeric_limits<std::size_t>::max() / kIndexEntrySize))
    {
        return FOL_ERROR_FORMAT;
    }

    FILE *output = nullptr;
#ifdef _WIN32
    _wfopen_s(&output, output_path.c_str(), L"wb");
#else
    output = std::fopen(output_path.c_str(), "wb");
#endif
    if (output == nullptr)
    {
        return FOL_ERROR_OPEN_OUTPUT;
    }

    const std::uint32_t header = static_cast<std::uint32_t>(final_entries.size()) | 0x80000000u;
    if (!write_exact(output, &header, sizeof(header)))
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }

    std::vector<std::uint8_t> blank_index(final_entries.size() * kIndexEntrySize, 0);
    if (!write_exact(output, blank_index.data(), blank_index.size()))
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }

    std::vector<std::uint8_t> index_buffer(final_entries.size() * kIndexEntrySize, 0);
    std::vector<std::uint32_t> keys_buffer(final_entries.size(), 0);
    std::uint32_t current_offset = static_cast<std::uint32_t>(sizeof(std::uint32_t) + blank_index.size());

    for (std::size_t i = 0; i < final_entries.size(); ++i)
    {
        keys_buffer[i] = final_entries[i].key;

        std::ifstream input(final_entries[i].path, std::ios::binary);
        if (!input)
        {
            std::fclose(output);
            return FOL_ERROR_OPEN_INPUT;
        }

        input.seekg(0, std::ios::end);
        const std::streamsize size = input.tellg();
        input.seekg(0, std::ios::beg);
        if (size < 0 || static_cast<std::uintmax_t>(size) > std::numeric_limits<std::uint32_t>::max())
        {
            std::fclose(output);
            return FOL_ERROR_READ;
        }
        if (static_cast<std::uintmax_t>(current_offset) + static_cast<std::uintmax_t>(size) > std::numeric_limits<std::uint32_t>::max())
        {
            std::fclose(output);
            return FOL_ERROR_FORMAT;
        }

        std::vector<std::uint8_t> content(static_cast<std::size_t>(size));
        if (size > 0)
        {
            input.read(reinterpret_cast<char *>(content.data()), size);
            if (!input.good() && !input.eof())
            {
                std::fclose(output);
                return FOL_ERROR_READ;
            }
            transform_content(content, final_entries[i].key, true);
            if (!write_exact(output, content.data(), content.size()))
            {
                std::fclose(output);
                return FOL_ERROR_WRITE;
            }
        }

        std::vector<std::uint8_t> index_entry(kIndexEntrySize, 0);
        if (final_entries[i].game_path.size() > 127)
        {
            std::fclose(output);
            return FOL_ERROR_FORMAT;
        }
        const std::size_t path_length = final_entries[i].game_path.size();
        std::memcpy(index_entry.data(), final_entries[i].game_path.data(), path_length);
        write_u32(index_entry.data() + 128, current_offset);
        write_u32(index_entry.data() + 132, static_cast<std::uint32_t>(size));
        transform_index(index_entry, final_entries[i].key, true);
        std::memcpy(index_buffer.data() + i * kIndexEntrySize, index_entry.data(), kIndexEntrySize);

        current_offset += static_cast<std::uint32_t>(size);

        const int progress = 35 + static_cast<int>((static_cast<double>(i + 1) / final_entries.size()) * 55.0);
        log_message(callback, user_data, progress, FOL_LOG_INFO, "Packed: " + log_text_from_game_path(final_entries[i].game_path));
    }

    if (std::fseek(output, static_cast<long>(sizeof(std::uint32_t)), SEEK_SET) != 0)
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }
    if (!write_exact(output, index_buffer.data(), index_buffer.size()))
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }

    if (std::fseek(output, 0, SEEK_END) != 0)
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }
    if (!write_exact(output, keys_buffer.data(), keys_buffer.size() * sizeof(std::uint32_t)))
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }

    std::vector<std::uint8_t> padding(kPaddingCount * sizeof(std::uint32_t), 0);
    if (!write_exact(output, padding.data(), padding.size()))
    {
        std::fclose(output);
        return FOL_ERROR_WRITE;
    }

    std::fclose(output);
    log_message(callback, user_data, 100, FOL_LOG_INFO, "Pack completed: " + utf8_from_path(output_path));
    return FOL_SUCCESS;
}

int fol_list(const char *input_fol, FolListCallback callback, void *user_data)
{
    if (input_fol == nullptr)
    {
        return FOL_ERROR_INVALID_ARGUMENT;
    }

    const fs::path input_path = path_from_utf8(input_fol);

    FILE *input = nullptr;
#ifdef _WIN32
    _wfopen_s(&input, input_path.c_str(), L"rb");
#else
    input = std::fopen(input_path.c_str(), "rb");
#endif
    if (input == nullptr)
    {
        return FOL_ERROR_OPEN_INPUT;
    }

    std::error_code file_size_error;
    const std::uintmax_t archive_size = fs::file_size(input_path, file_size_error);
    if (file_size_error || archive_size < sizeof(std::uint32_t) + kPaddingCount * sizeof(std::uint32_t))
    {
        std::fclose(input);
        return FOL_ERROR_FORMAT;
    }

    std::int32_t raw_count = 0;
    if (!read_exact(input, &raw_count, sizeof(raw_count)))
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    const bool encrypted = raw_count < 0;
    const int file_count = raw_count & 0x7FFFFFFF;
    if (!encrypted || file_count <= 0)
    {
        std::fclose(input);
        return FOL_ERROR_FORMAT;
    }

    const std::uintmax_t index_size = static_cast<std::uintmax_t>(file_count) * kIndexEntrySize;
    const std::uintmax_t key_table_size = static_cast<std::uintmax_t>(file_count) * sizeof(std::uint32_t);
    const std::uintmax_t footer_size = key_table_size + kPaddingCount * sizeof(std::uint32_t);
    const std::uintmax_t data_base_wide = sizeof(std::uint32_t) + index_size;
    if (index_size > std::numeric_limits<std::size_t>::max() ||
        data_base_wide > std::numeric_limits<std::uint32_t>::max() ||
        footer_size > archive_size ||
        data_base_wide > archive_size - footer_size)
    {
        std::fclose(input);
        return FOL_ERROR_FORMAT;
    }

    std::vector<std::uint32_t> keys(static_cast<std::size_t>(file_count));
    if (std::fseek(input, -static_cast<long>(footer_size), SEEK_END) != 0 ||
        !read_exact(input, keys.data(), keys.size() * sizeof(std::uint32_t)))
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    if (std::fseek(input, static_cast<long>(sizeof(std::uint32_t)), SEEK_SET) != 0)
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }

    std::vector<std::uint8_t> index_data(static_cast<std::size_t>(index_size));
    if (!read_exact(input, index_data.data(), index_data.size()))
    {
        std::fclose(input);
        return FOL_ERROR_READ;
    }
    std::fclose(input);

    const std::uint32_t data_base = static_cast<std::uint32_t>(data_base_wide);
    for (int i = 0; i < file_count; ++i)
    {
        std::vector<std::uint8_t> entry(kIndexEntrySize);
        std::memcpy(entry.data(), index_data.data() + static_cast<std::size_t>(i) * kIndexEntrySize, kIndexEntrySize);

        const std::uint32_t key = keys[static_cast<std::size_t>(i)];
        transform_index(entry, key, false);

        char name_buffer[129] = {};
        std::memcpy(name_buffer, entry.data(), 128);
        const std::string game_path(name_buffer);

        std::uint32_t offset = read_u32(entry.data() + 128);
        std::uint32_t size = read_u32(entry.data() + 132);
        if (offset < data_base && offset > 0)
        {
            offset += data_base;
        }

        if (callback != nullptr)
        {
            callback(log_text_from_game_path(game_path).c_str(), size, user_data);
        }
    }

    return FOL_SUCCESS;
}

const char *fol_result_message(int code)
{
    switch (code)
    {
    case FOL_SUCCESS:
        return "success";
    case FOL_ERROR_INVALID_ARGUMENT:
        return "invalid argument";
    case FOL_ERROR_OPEN_INPUT:
        return "failed to open input";
    case FOL_ERROR_OPEN_OUTPUT:
        return "failed to open output";
    case FOL_ERROR_READ:
        return "failed to read file content";
    case FOL_ERROR_WRITE:
        return "failed to write file content";
    case FOL_ERROR_FORMAT:
        return "invalid or unsupported fol format";
    case FOL_ERROR_MANIFEST:
        return "invalid workspace";
    case FOL_ERROR_FILESYSTEM:
        return "filesystem operation failed";
    default:
        return "unknown error";
    }
}
