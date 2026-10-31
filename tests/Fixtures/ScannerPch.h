// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#pragma once

// Exercise the production scanner against an ordinary fixture file, without UI or elevation.
#pragma include_alias("pch.h", "ScannerPch.h")
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <shlobj.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <execution>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include "SmartPointer.h"
#include "BlockingQueue.h"

namespace wds
{
    constexpr auto Ki = 1024ull;
    constexpr auto Mi = Ki * Ki;
}
constexpr DWORD IO_REPARSE_TAG_JUNCTION_POINT = ~IO_REPARSE_TAG_MOUNT_POINT;
template<typename T>
constexpr T* ByteOffset(void* ptr, const std::ptrdiff_t offset) noexcept
{
    return reinterpret_cast<T*>(static_cast<BYTE*>(ptr) + offset);
}
inline std::wstring TranslateError(DWORD error) { return std::to_wstring(error); }
#define VTRACE(...) ((void)0)

struct COptions
{
    inline static bool ExcludeDropboxIgnored = true;
    struct ThreadSetting
    {
        int value = 4;
        int Obj() const { return value; }
    };
    static ThreadSetting ScanningThreads;
};
inline COptions::ThreadSetting COptions::ScanningThreads;

class CItem
{
    std::wstring m_path;
    ULONGLONG m_index = 0;

public:

    explicit CItem(std::wstring path, ULONGLONG index = 0) : m_path(std::move(path)), m_index(index) {}
    std::wstring GetPathLong() const { return m_path; }
    std::wstring GetPath() const { return m_path; }
    ULONGLONG GetIndex() const { return m_index; }
    void SetIndex(ULONGLONG index) { m_index = index; }
    void UpwardDrivePacman() const {}
};
