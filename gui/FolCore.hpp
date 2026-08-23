#pragma once

#include "fol_core.h"

#include <cstdint>
#include <functional>
#include <string>

// 日志回调：progress 0-100，level 为核心日志级别，msg 已本地化
using LogCallback = std::function<void(int progress, FolLogLevel level, const std::wstring& msg)>;

// 归档内容预览回调：path 为本地形式路径，size 为解包后字节数
using ListCallback = std::function<void(const std::wstring& path, std::uint32_t size)>;

class FolCore
{
public:
    static int Unpack(const std::wstring& inputFol, const std::wstring& outputDir, LogCallback logger);
    static int Pack(const std::wstring& inputDir, const std::wstring& outputFol, LogCallback logger);
    static int List(const std::wstring& inputFol, const ListCallback& onEntry, LogCallback logger);
};
