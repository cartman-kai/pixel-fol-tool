#define _CRT_SECURE_NO_WARNINGS
#include "FolCore.hpp"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>

namespace fs = std::filesystem;

// 常量定义
const int INDEX_ENTRY_SIZE = 136;
const int PADDING_COUNT = 97;

std::atomic<bool> FolCore::g_stopRequested = false;

// ==========================================
// 编码转换：核心部分
// ==========================================
std::string FolCore::WideToGB2312(const std::wstring& wstr)
{
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(936, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    std::string str(len - 1, 0); // -1 是去掉 null 终止符，因为 string 管理长度
    WideCharToMultiByte(936, 0, wstr.c_str(), -1, &str[0], len, NULL, NULL);
    return str;
}

std::wstring FolCore::GB2312ToWide(const std::string& str)
{
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(936, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(len - 1, 0);
    MultiByteToWideChar(936, 0, str.c_str(), -1, &wstr[0], len);
    return wstr;
}

// ==========================================
// 算法部分 (保持原样，改用 vector)
// ==========================================
void FolCore::TransformContent(std::vector<uint8_t>& data, uint32_t key, bool isEncrypt)
{
    size_t numInts = data.size() / 4;
    uint32_t* ints = reinterpret_cast<uint32_t*>(data.data());
    for (size_t i = 0; i < numInts; i++)
    {
        uint32_t term = 99 * (uint32_t)(i * i);
        if (isEncrypt) ints[i] = ints[i] + key + term;
        else ints[i] = ints[i] - key - term;
    }
}

void FolCore::TransformIndex(std::vector<uint8_t>& data, uint32_t key, bool isEncrypt)
{
    uint32_t* ints = reinterpret_cast<uint32_t*>(data.data());
    for (int i = 0; i < 34; i++) // 136 / 4
    {
        uint32_t term = 9 * (uint32_t)(i * i * i);
        if (isEncrypt) ints[i] = ints[i] + key + term;
        else ints[i] = ints[i] - key - term;
    }
}

// ==========================================
// 解包逻辑 (Unpack)
// ==========================================
void FolCore::Unpack(const std::wstring& inputFol, const std::wstring& outputDir, LogCallback logger)
{
    FILE* f = _wfopen(inputFol.c_str(), L"rb");
    if (!f) {
        logger(0, L"Error: 无法打开 FOL 文件: " + inputFol);
        return;
    }

    int32_t rawCount;
    if (fread(&rawCount, 4, 1, f) != 1) {
        fclose(f); logger(0, L"Error: 读取文件头失败"); return;
    }

    bool isEncrypted = rawCount < 0;
    int count = rawCount & 0x7FFFFFFF;

    if (!isEncrypted) {
        fclose(f); logger(0, L"Error: 文件未加密或格式不正确"); return;
    }

    logger(5, L"文件检查通过，包含文件数量: " + std::to_wstring(count));

    // 创建目录结构
    fs::path rootPath(outputDir);
    fs::path assetsPath = rootPath / "assets";
    fs::create_directories(assetsPath);

    // 读取 Keys
    fseek(f, -4 * (PADDING_COUNT + count), SEEK_END);
    std::vector<uint32_t> keys(count);
    fread(keys.data(), 4, count, f);

    // 读取 Index
    fseek(f, 4, SEEK_SET);
    std::vector<uint8_t> idxData(count * INDEX_ENTRY_SIZE);
    fread(idxData.data(), 1, idxData.size(), f);

    uint32_t dataBaseOffset = 4 + (uint32_t)idxData.size();

    // 准备 Manifest 内容
    std::vector<std::string> manifestLines;
    manifestLines.push_back("# FOL Manifest");
    manifestLines.push_back("# Format: Index|Key|GamePath");

    int processed = 0;
    for (int i = 0; i < count; i++)
    {
        if (g_stopRequested) break;

        uint32_t key = keys[i];
        std::vector<uint8_t> entry(INDEX_ENTRY_SIZE);
        memcpy(entry.data(), idxData.data() + i * INDEX_ENTRY_SIZE, INDEX_ENTRY_SIZE);

        TransformIndex(entry, key, false);

        // 获取文件名 (GB2312)
        char nameBuf[129] = { 0 };
        memcpy(nameBuf, entry.data(), 128);
        std::string gamePathGB = nameBuf; // 游戏内部路径
        std::wstring gamePathW = GB2312ToWide(gamePathGB); // 转换为宽字符用于本地文件系统

        uint32_t offset = *reinterpret_cast<uint32_t*>(entry.data() + 128);
        uint32_t size = *reinterpret_cast<uint32_t*>(entry.data() + 132);

        if (offset < dataBaseOffset) offset += dataBaseOffset;

        // 添加到 Manifest
        char manifestLine[512];
        sprintf(manifestLine, "%d|%u|%s", i, key, gamePathGB.c_str());
        manifestLines.push_back(manifestLine);

        // 提取文件
        if (size > 0)
        {
            fseek(f, offset, SEEK_SET);
            std::vector<uint8_t> fileContent(size);
            fread(fileContent.data(), 1, size, f);
            TransformContent(fileContent, key, false);

            // 拼接本地路径，注意处理反斜杠
            std::wstring localRelPath = gamePathW;
            std::replace(localRelPath.begin(), localRelPath.end(), L'/', L'\\');
            fs::path finalPath = assetsPath / localRelPath;

            // 确保子目录存在
            fs::create_directories(finalPath.parent_path());

            FILE* fout = _wfopen(finalPath.c_str(), L"wb");
            if (fout) {
                fwrite(fileContent.data(), 1, size, fout);
                fclose(fout);
            }
        }

        processed++;
        if (processed % 10 == 0 || processed == count) {
            int progress = 10 + (int)((float)processed / count * 90);
            logger(progress, L"提取中: " + gamePathW);
        }
    }

    // 写 Manifest.txt
    std::ofstream mfs(rootPath / "manifest.txt");
    for (const auto& line : manifestLines) mfs << line << std::endl;
    mfs.close();

    fclose(f);
    logger(100, L"解包完成！");
}

// ==========================================
// 打包逻辑 (Pack)
// ==========================================
struct PackEntry {
    std::string gamePath; // GB2312
    std::wstring diskPath; // Unicode
    uint32_t key;
    int originalIndex;
    uint32_t size;
};

void FolCore::Pack(const std::wstring& inputDir, const std::wstring& outputFol, LogCallback logger)
{
    fs::path workDir(inputDir);
    fs::path assetsDir = workDir / "assets";
    fs::path manifestPath = workDir / "manifest.txt";

    if (!fs::exists(assetsDir) || !fs::exists(manifestPath)) {
        logger(0, L"Error: 找不到 assets 目录或 manifest.txt 文件。请确认目录正确。");
        return;
    }

    // 1. 读取 Manifest
    std::map<std::string, std::pair<int, uint32_t>> manifestMap;
    std::ifstream mfs(manifestPath);
    std::string line;
    while (std::getline(mfs, line)) {
        if (line.empty() || line[0] == '#') continue;
        // 简单解析 split
        size_t p1 = line.find('|');
        size_t p2 = line.find('|', p1 + 1);
        if (p1 != std::string::npos && p2 != std::string::npos) {
            int idx = std::stoi(line.substr(0, p1));
            uint32_t k = (uint32_t)std::stoul(line.substr(p1 + 1, p2 - p1 - 1));
            std::string path = line.substr(p2 + 1);
            // 去除可能的换行符
            while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) path.pop_back();
            manifestMap[path] = { idx, k };
        }
    }
    mfs.close();

    logger(10, L"Manifest 读取完毕，记录数: " + std::to_wstring(manifestMap.size()));

    // 2. 扫描文件
    std::vector<PackEntry> files;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
            if (entry.is_regular_file()) {
                if (entry.path().filename() == ".DS_Store") continue;

                PackEntry pe;
                pe.diskPath = entry.path().wstring();
                pe.size = (uint32_t)entry.file_size();

                // 计算相对路径并转为 GB2312
                std::wstring rel = fs::relative(entry.path(), assetsDir).wstring();
                std::replace(rel.begin(), rel.end(), L'/', L'\\'); // 统一为 Windows 反斜杠
                pe.gamePath = WideToGB2312(rel);

                // 查找 Key
                if (manifestMap.count(pe.gamePath)) {
                    pe.originalIndex = manifestMap[pe.gamePath].first;
                    pe.key = manifestMap[pe.gamePath].second;
                }
                else {
                    pe.originalIndex = 999999; // 新文件放到最后
                    pe.key = (rand() & 0xFFFF) | ((rand() & 0xFFFF) << 16);
                    logger(10, L"发现新文件(自动生成Key): " + rel);
                }
                files.push_back(pe);
            }
        }
    }
    catch (const std::exception& ex) {
        logger(0, L"Error: 扫描目录时出错: " + GB2312ToWide(ex.what()));
        return;
    }

    // 排序
    std::sort(files.begin(), files.end(), [](const PackEntry& a, const PackEntry& b) {
        if (a.originalIndex != b.originalIndex) return a.originalIndex < b.originalIndex;
        return a.gamePath < b.gamePath;
        });

    // 3. 写入文件
    FILE* fo = _wfopen(outputFol.c_str(), L"wb");
    if (!fo) {
        logger(0, L"Error: 无法创建输出文件: " + outputFol);
        return;
    }

    int count = (int)files.size();
    uint32_t head = count | 0x80000000;
    fwrite(&head, 4, 1, fo);

    // 占位索引
    std::vector<uint8_t> blankIndex(count * INDEX_ENTRY_SIZE, 0);
    fwrite(blankIndex.data(), 1, blankIndex.size(), fo);

    std::vector<uint8_t> idxBuffer(count * INDEX_ENTRY_SIZE);
    uint32_t currentOffset = 4 + (uint32_t)blankIndex.size();
    std::vector<uint32_t> keysBuffer;

    for (int i = 0; i < count; i++)
    {
        if (g_stopRequested) break;

        PackEntry& p = files[i];
        keysBuffer.push_back(p.key);

        // 读源文件
        FILE* fi = _wfopen(p.diskPath.c_str(), L"rb");
        if (!fi) {
            logger(i, L"Warning: 读取源文件失败: " + p.diskPath);
            continue;
        }
        std::vector<uint8_t> content(p.size);
        fread(content.data(), 1, p.size, fi);
        fclose(fi);

        // 加密
        TransformContent(content, p.key, true);
        fwrite(content.data(), 1, content.size(), fo);

        // 构建索引
        std::vector<uint8_t> idxEntry(INDEX_ENTRY_SIZE, 0);

        // 文件名 (GB2312)
        strncpy((char*)idxEntry.data(), p.gamePath.c_str(), 127);

        *reinterpret_cast<uint32_t*>(idxEntry.data() + 128) = currentOffset;
        *reinterpret_cast<uint32_t*>(idxEntry.data() + 132) = p.size;

        TransformIndex(idxEntry, p.key, true);
        memcpy(idxBuffer.data() + i * INDEX_ENTRY_SIZE, idxEntry.data(), INDEX_ENTRY_SIZE);

        currentOffset += (uint32_t)content.size();

        if (i % 5 == 0) logger(20 + (int)((float)i / count * 80), L"打包中: " + GB2312ToWide(p.gamePath));
    }

    // 回写索引
    fseek(fo, 4, SEEK_SET);
    fwrite(idxBuffer.data(), 1, idxBuffer.size(), fo);

    // 写入 Key 表
    fseek(fo, 0, SEEK_END);
    fwrite(keysBuffer.data(), 4, keysBuffer.size(), fo);

    // Padding
    std::vector<uint8_t> padding(PADDING_COUNT * 4, 0);
    fwrite(padding.data(), 1, padding.size(), fo);

    fclose(fo);
    logger(100, L"打包成功！已生成: " + outputFol);
}