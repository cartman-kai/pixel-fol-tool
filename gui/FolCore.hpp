#pragma once

#include "fol_core.h"

#include <functional>
#include <string>

using LogCallback = std::function<void(int progress, const std::wstring& msg)>;

class FolCore
{
public:
    static int Unpack(const std::wstring& inputFol, const std::wstring& outputDir, LogCallback logger);
    static int Pack(const std::wstring& inputDir, const std::wstring& outputFol, LogCallback logger);
};
