#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>

// 定义日志回调类型：函数接受两个参数(进度0-100, 文本消息)
using LogCallback = std::function<void(int progress, const std::wstring& msg)>;

class FolCore
{
public:
    // 解包
    // inputFol: .fol 文件完整路径
    // outputDir: 输出目录路径
    static void Unpack(const std::wstring& inputFol, const std::wstring& outputDir, LogCallback logger);

    // 打包
    // inputDir: 包含 assets 和 manifest.txt 的目录
    // outputFol: 目标 .fol 文件路径
    static void Pack(const std::wstring& inputDir, const std::wstring& outputFol, LogCallback logger);

    // 停止标志（用于以后扩展“取消”功能）
    static std::atomic<bool> g_stopRequested;

private:
    // 编码转换辅助函数
    static std::string WideToGB2312(const std::wstring& wstr);
    static std::wstring GB2312ToWide(const std::string& str);

    // 加解密算法
    static void TransformContent(std::vector<uint8_t>& data, uint32_t key, bool isEncrypt);
    static void TransformIndex(std::vector<uint8_t>& data, uint32_t key, bool isEncrypt);
};