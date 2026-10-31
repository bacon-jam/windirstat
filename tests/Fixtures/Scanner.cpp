// WinDirStat - Windows Directory Statistics
// Copyright © WinDirStat Team
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Distributed WITHOUT ANY WARRANTY; see LICENSE.md for details.

#include "pch.h"

namespace Fixture
{
    std::wstring path;
    ULONG records = 0;
    ULONG fragments = 1;
    bool separated = false;
    bool skewed = false;
    bool seekPenalty = false;
    bool seekQuerySupported = true;
    std::atomic<ULONG> reads = 0;
    std::atomic<ULONG> workers = 0;
    ULONG failRead = 0;
    BlockingQueue<CItem*>* cancelQueue = nullptr;
    HANDLE readEntered = nullptr;
    HANDLE readRelease = nullptr;

    LONGLONG ExtentStart(const ULONG index)
    {
        if (skewed && fragments > 1 && index > 0)
        {
            const LONGLONG first = static_cast<LONGLONG>(records / 4) * 7 / 8;
            return first + (records / 4 - first) * (index - 1) / (fragments - 1);
        }
        return static_cast<LONGLONG>(records / 4) * index / fragments;
    }

    LONGLONG ExtentLcn(const ULONG index)
    {
        // Leave a 1 MiB gap between runs so the benchmark exercises discontinuous reads.
        return 2 + ExtentStart(index) + (separated ? static_cast<LONGLONG>(index) * 256 : 0);
    }

    HANDLE WINAPI Open(LPCWSTR, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
        DWORD creation, DWORD flags, HANDLE fileTemplate)
    {
        return CreateFileW(path.c_str(), access, sharing, security, creation, flags, fileTemplate);
    }

    HANDLE WINAPI Event(LPSECURITY_ATTRIBUTES security, BOOL manualReset, BOOL initialState, LPCWSTR name)
    {
        // Each read worker creates one private event; measurements do not pause or retry workers.
        ++workers;
        return CreateEventW(security, manualReset, initialState, name);
    }

    BOOL WINAPI Control(HANDLE, DWORD code, LPVOID, DWORD, LPVOID output, DWORD outputSize,
        LPDWORD returned, LPOVERLAPPED)
    {
        if (code == FSCTL_GET_NTFS_VOLUME_DATA)
        {
            auto& info = *static_cast<NTFS_VOLUME_DATA_BUFFER*>(output);
            info = {};
            info.BytesPerSector = 512;
            info.BytesPerCluster = 4096;
            info.BytesPerFileRecordSegment = 1024;
            info.MftValidDataLength.QuadPart = static_cast<LONGLONG>(records) * 1024;
            *returned = sizeof(info);
            return true;
        }
        if (code == FSCTL_GET_RETRIEVAL_POINTERS)
        {
            const DWORD bytes = static_cast<DWORD>(offsetof(RETRIEVAL_POINTERS_BUFFER, Extents) +
                fragments * sizeof(RETRIEVAL_POINTERS_BUFFER::Extents[0]));
            if (outputSize < bytes)
            {
                SetLastError(ERROR_MORE_DATA);
                return false;
            }
            auto& pointers = *static_cast<RETRIEVAL_POINTERS_BUFFER*>(output);
            pointers.ExtentCount = fragments;
            pointers.StartingVcn.QuadPart = 0;
            for (ULONG i = 0; i < fragments; ++i)
            {
                pointers.Extents[i].Lcn.QuadPart = ExtentLcn(i);
                pointers.Extents[i].NextVcn.QuadPart = ExtentStart(i + 1);
            }
            *returned = bytes;
            return true;
        }
        if (code == IOCTL_STORAGE_QUERY_PROPERTY && seekQuerySupported)
        {
            auto& descriptor = *static_cast<DEVICE_SEEK_PENALTY_DESCRIPTOR*>(output);
            descriptor = {};
            descriptor.Size = sizeof(descriptor);
            descriptor.IncursSeekPenalty = seekPenalty;
            *returned = sizeof(descriptor);
            return true;
        }
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    BOOL WINAPI Read(HANDLE file, LPVOID buffer, DWORD size, LPDWORD returned, LPOVERLAPPED overlapped)
    {
        const auto count = ++reads;
        if (count == 1 && readEntered != nullptr)
        {
            SetEvent(readEntered);
            WaitForSingleObject(readRelease, 10000);
        }
        if (count == failRead)
        {
            SetLastError(ERROR_READ_FAULT);
            return false;
        }
        if (count == (records > 4096 ? 2u : 1u) && cancelQueue != nullptr) cancelQueue->CancelExecution();
        return ReadFile(file, buffer, size, returned, overlapped);
    }
}

// Only volume discovery is replaced.
// Reads use real overlapped, unbuffered file I/O; parsing, partitioning, merging and enumeration come directly from the shipping source.
#undef CreateFile
#undef CreateEvent
#define CreateFile Fixture::Open
#define CreateEvent Fixture::Event
#define DeviceIoControl Fixture::Control
#define ReadFile Fixture::Read
#include "FinderNtfs.cpp"
#undef ReadFile
#undef DeviceIoControl
#undef CreateFile
#undef CreateEvent

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

bool IsDirectory(const ULONG index) { return index == 5 || index >= 16 && index % 64 == 0; }
bool IsCrossPartitionExtension(const ULONG index) { return index == Fixture::records / 4 + 17; }
bool IsExtension(const ULONG index)
{
    return IsCrossPartitionExtension(index) || index >= 16 && index % 257 == 0 && index % 64 > 1;
}
bool IsUsed(const ULONG index)
{
    return IsDirectory(index) || IsCrossPartitionExtension(index) || index >= 16 && index % 23 != 0;
}
ULONGLONG LogicalSize(const ULONG index) { return static_cast<ULONGLONG>(index) * 4099 + 31; }
ULONGLONG PhysicalSize(const ULONG index) { return (LogicalSize(index) + 4095) & ~4095ull; }
ULONG Parent(const ULONG index) { return IsDirectory(index) || index < 64 ? 5 : index / 64 * 64; }

void MakeFixture(const std::filesystem::path& path, const ULONG records)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(output), "Create fixture file");
    std::vector<char> block(1024 * 1024);
    output.write(block.data(), 8192);
    for (ULONG first = 0; first < records; first += 1024)
    {
        std::ranges::fill(block, '\0');
        for (ULONG index = first; index < std::min(records, first + 1024); ++index)
        {
            auto* bytes = block.data() + (index - first) * 1024;
            auto& record = *reinterpret_cast<FILE_RECORD*>(bytes);
            record.Signature = 0x454C4946;
            record.UsaOffset = sizeof(FILE_RECORD);
            record.UsaCount = 3;
            record.SequenceNumber = 7;
            record.Flags = (IsUsed(index) ? 1 : 0) | (IsDirectory(index) ? 2 : 0);
            record.FirstAttributeOffset = 56;
            record.BytesAvailable = 1024;
            record.SegmentNumberLowPart = index;
            const bool extension = IsExtension(index);
            if (extension)
            {
                record.BaseFileRecordNumber = IsCrossPartitionExtension(index) ? 17 : index - 1;
                record.BaseFileRecordSequence = 7;
            }
            ULONG cursor = record.FirstAttributeOffset;
            const auto resident = [&](ATTRIBUTE_TYPE_CODE type, const void* value, const ULONG size)
            {
                auto* attribute = reinterpret_cast<ATTRIBUTE_RECORD*>(bytes + cursor);
                attribute->TypeCode = type;
                attribute->RecordLength = (24 + size + 7) & ~7u;
                attribute->Form.Resident.ValueOffset = 24;
                attribute->Form.Resident.ValueLength = size;
                memcpy(bytes + cursor + 24, value, size);
                cursor += attribute->RecordLength;
            };
            const auto addName = [&](const ULONG parent, const std::wstring& name)
            {
                std::vector<BYTE> storage(offsetof(FILE_NAME, FileName) + name.size() * sizeof(wchar_t));
                auto* fn = reinterpret_cast<FILE_NAME*>(storage.data());
                fn->ParentDirectory = parent;
                fn->ParentSequence = 7;
                fn->FileNameLength = static_cast<UCHAR>(name.size());
                fn->Flags = 1;
                memcpy(fn->FileName, name.data(), name.size() * sizeof(wchar_t));
                resident(AttributeFileName, storage.data(), static_cast<ULONG>(storage.size()));
            };
            if (!extension)
            {
                STANDARD_INFORMATION info{};
                info.FileAttributes = IsDirectory(index) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
                info.LastModificationTime = { index, 1 };
                resident(AttributeStandardInformation, &info, sizeof(info));
                if (index != 5) addName(Parent(index), std::format(L"file-{}-scanner-fixture.dat", index));
                if (!IsDirectory(index) && index % 101 == 0)
                    addName(5, std::format(L"alias-{}-scanner-fixture.dat", index));
            }
            if (IsCrossPartitionExtension(index)) addName(5, L"alias-17-scanner-fixture.dat");
            if (!IsDirectory(index) && index != 17)
            {
                auto* data = reinterpret_cast<ATTRIBUTE_RECORD*>(bytes + cursor);
                data->TypeCode = AttributeData;
                data->RecordLength = 72;
                data->FormCode = 1;
                const ULONG owner = IsCrossPartitionExtension(index) ? 17 : extension ? index - 1 : index;
                data->Form.Nonresident.FileSize = LogicalSize(owner);
                data->Form.Nonresident.AllocatedLength = PhysicalSize(owner);
                cursor += data->RecordLength;
            }
            *reinterpret_cast<ULONG*>(bytes + cursor) = AttributeEnd;
            record.FirstFreeByte = cursor + 8;
            auto* fixups = reinterpret_cast<USHORT*>(bytes + record.UsaOffset);
            fixups[0] = 0xA55A;
            for (ULONG sector = 1; sector <= 2; ++sector)
            {
                auto* tail = reinterpret_cast<USHORT*>(bytes + sector * 512 - 2);
                fixups[sector] = *tail;
                *tail = fixups[0];
            }
        }
        const ULONG last = std::min(records, first + 1024);
        for (ULONG extent = 0; extent < Fixture::fragments; ++extent)
        {
            const auto start = std::max<LONGLONG>(first, Fixture::ExtentStart(extent) * 4);
            const auto end = std::min<LONGLONG>(last, Fixture::ExtentStart(extent + 1) * 4);
            if (start >= end) continue;
            output.seekp(Fixture::ExtentLcn(extent) * 4096 + (start - Fixture::ExtentStart(extent) * 4) * 1024);
            output.write(block.data() + (start - first) * 1024, (end - start) * 1024);
        }
    }
    output.flush();
    Require(static_cast<bool>(output), "Write fixture file");
}

ULONGLONG Verify(FinderNtfsContext& context, const ULONG records)
{
    FinderNtfs finder(&context);
    ULONGLONG count = 0;
    std::vector<BYTE> seen(records);
    for (ULONG parent = 5; parent < records; parent = parent == 5 ? 64 : parent + 64)
    {
        CItem directory(L"fixture", parent);
        for (bool found = finder.FindFile(&directory); found; found = finder.FindNext())
        {
            const auto reference = finder.GetIndex();
            const auto index = static_cast<ULONG>(reference & FinderNtfsContext::NtfsRecordMask);
            const bool alias = finder.GetFileName().starts_with(L"alias-");
            Require(index >= 16 && index < records && IsUsed(index) && !IsExtension(index), "Unexpected record");
            const BYTE flag = alias ? 2 : 1;
            Require((seen[index] & flag) == 0, "Duplicate filename");
            seen[index] |= flag;
            Require(reference >> 48 == 7, "File-reference sequence");
            Require(finder.GetFileName() == std::format(L"{}-{}-scanner-fixture.dat", alias ? L"alias" : L"file", index), "Filename arena");
            Require(parent == (alias ? 5 : Parent(index)), "Parent directory");
            Require(finder.GetFileSizeLogical() == (IsDirectory(index) ? 0 : LogicalSize(index)), "Logical size");
            Require(finder.GetFileSizePhysical() == (IsDirectory(index) ? 0 : PhysicalSize(index)), "Physical size");
            Require(finder.IsDirectory() == IsDirectory(index), "Directory attribute");
            Require(finder.GetLastWriteTime().dwLowDateTime == index, "Modification time");
            ++count;
        }
        Require(finder.GetResult().outcome == ScanOutcome::Complete, "Complete enumeration");
    }
    ULONGLONG expected = 0;
    for (ULONG index = 16; index < records; ++index)
        if (IsUsed(index) && !IsExtension(index)) expected += 1 + (!IsDirectory(index) && (index == 17 || index % 101 == 0));
    Require(count == expected, "Every filename and hardlink appears exactly once");
    return count;
}

void TestPauseOrCancel(const bool cancel)
{
    Fixture::reads = 0;
    const SmartPointer entered(CloseHandle, CreateEventW(nullptr, true, false, nullptr));
    const SmartPointer release(CloseHandle, CreateEventW(nullptr, true, false, nullptr));
    Require(entered != nullptr && release != nullptr, "Create read gates");
    Fixture::readEntered = entered;
    Fixture::readRelease = release;
    CItem root(L"fixture");
    FinderNtfsContext context;
    bool loaded = false;
    BlockingQueue<CItem*> queue;
    queue.Push(&root);
    queue.StartThreads(1, [&]
    {
        for (auto item = queue.Pop(); item.has_value(); item = queue.Pop())
            loaded = context.LoadRoot(item.value(), &queue);
    });
    const bool started = WaitForSingleObject(entered, 10000) == WAIT_OBJECT_0;
    std::jthread requester([&]
    {
        if (cancel) queue.CancelExecution();
        else queue.SuspendExecution();
    });
    const auto deadline = GetTickCount64() + 10000;
    while (!queue.IsPauseOrCancelRequested() && GetTickCount64() < deadline) Sleep(1);
    SetEvent(release);
    requester.join();
    const auto pausedReads = Fixture::reads.load();
    Sleep(20);
    const bool drained = pausedReads == Fixture::reads.load();
    queue.ResumeExecution();
    queue.JoinThreads();
    Fixture::readEntered = nullptr;
    Fixture::readRelease = nullptr;
    Require(started && drained, "Pause/cancel drains reads before acknowledging the request");
    Require(loaded == !cancel && context.IsLoaded() == loaded, "Pause resumes; cancellation leaves no published index");
    if (loaded) Verify(context, Fixture::records);
    std::cout << (cancel ? "PASS cancel during parallel read\n" : "PASS pause/resume during parallel read\n") << std::flush;
}

double ProcessSeconds()
{
    FILETIME creation, exit, kernel, user;
    Require(GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user), "Read process CPU time");
    return ((static_cast<ULONGLONG>(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime +
        (static_cast<ULONGLONG>(user.dwHighDateTime) << 32) + user.dwLowDateTime) / 10000000.0;
}

void Measure()
{
    BlockingQueue<CItem*> queue;
    CItem root(L"fixture");
    FinderNtfsContext context;
    const auto cpuStart = ProcessSeconds();
    const auto start = std::chrono::steady_clock::now();
    Require(context.LoadRoot(&root, &queue), "Load benchmark MFT");
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto cpuSeconds = ProcessSeconds() - cpuStart;
    PROCESS_MEMORY_COUNTERS memory{ sizeof(memory) };
    Require(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)), "Read peak memory");
    const auto entries = Verify(context, Fixture::records);
    std::cout << std::format("{{\"records\":{},\"fragments\":{},\"threads\":{},\"entries\":{},"
        "\"reads\":{},\"workers\":{},\"seconds\":{:.9f},\"cpu_seconds\":{:.9f},\"peak_working_set_bytes\":{}}}\n",
        Fixture::records, Fixture::fragments, COptions::ScanningThreads.Obj(), entries,
        Fixture::reads.load(), Fixture::workers.load(), seconds, cpuSeconds, memory.PeakWorkingSetSize);
}

int wmain(int argc, wchar_t* argv[])
{
    try
    {
        Require(argc >= 2, "Usage: ScannerTests <new-fixture-file> [record-count] [repetitions], or --create/--measure");
        const std::wstring_view command = argv[1];
        if (command == L"--create" || command == L"--measure")
        {
            const int arguments = command == L"--create" ? 5 : 7;
            Require(argc == arguments || argc == arguments + 1,
                "--create <path> <records> <fragments> [balanced|skewed]; "
                "--measure <path> <records> <fragments> <threads> <ssd|hdd|unknown> [balanced|skewed]");
            Fixture::path = argv[2];
            Fixture::records = std::stoul(argv[3]);
            Fixture::fragments = std::stoul(argv[4]);
            Fixture::separated = true;
            const std::wstring_view layout = argc > arguments ? argv[arguments] : L"balanced";
            Require(layout == L"balanced" || layout == L"skewed", "Valid extent layout");
            Fixture::skewed = layout == L"skewed";
            Require(Fixture::records >= 1024 && Fixture::records <= 4194304 && Fixture::records % 1024 == 0 &&
                Fixture::fragments > 0 && Fixture::fragments <= 37, "Valid benchmark dimensions");
            if (command == L"--create")
            {
                Require(!std::filesystem::exists(Fixture::path), "Fixture path must not already exist");
                MakeFixture(Fixture::path, Fixture::records);
            }
            else
            {
                COptions::ScanningThreads.value = std::stoi(argv[5]);
                const std::wstring_view device = argv[6];
                Require(COptions::ScanningThreads.value > 0 && COptions::ScanningThreads.value <= 64 &&
                    (device == L"ssd" || device == L"hdd" || device == L"unknown"), "Valid scan settings");
                Fixture::seekPenalty = device == L"hdd";
                Fixture::seekQuerySupported = device != L"unknown";
                const auto bytes = (Fixture::ExtentLcn(Fixture::fragments - 1) + Fixture::records / 4 -
                    Fixture::ExtentStart(Fixture::fragments - 1)) * 4096;
                Require(std::filesystem::file_size(Fixture::path) == static_cast<ULONGLONG>(bytes), "Fixture file size");
                Measure();
            }
            return 0;
        }
        Fixture::path = argv[1];
        Fixture::records = argc > 2 ? std::stoul(argv[2]) : 262144;
        const int repetitions = argc > 3 ? std::stoi(argv[3]) : 1;
        Require(Fixture::records >= 1024 && Fixture::records % 1024 == 0 && repetitions > 0, "Valid fixture dimensions");
        Require(!std::filesystem::exists(Fixture::path), "Fixture path must not already exist");
        MakeFixture(Fixture::path, Fixture::records);
        for (const ULONG fragments : { 1u, 3u, 37u })
        {
            Fixture::fragments = fragments;
            for (const int threads : { 1, 4 }) for (int iteration = 0; iteration < repetitions; ++iteration)
            {
                COptions::ScanningThreads.value = threads;
                Fixture::reads = 0;
                BlockingQueue<CItem*> queue;
                CItem root(L"fixture");
                FinderNtfsContext context;
                const auto start = std::chrono::steady_clock::now();
                Require(context.LoadRoot(&root, &queue), "Load fixture MFT");
                const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                const auto count = Verify(context, Fixture::records);
                std::cout << std::format("PASS fragments={} threads={} iteration={} records={} entries={} reads={} seconds={:.6f}\n",
                    fragments, threads, iteration, Fixture::records, count, Fixture::reads.load(), seconds) << std::flush;
            }
        }
        Fixture::fragments = 1;
        for (int mode = 0; mode < 4; ++mode)
        {
            Fixture::reads = 0;
            Fixture::seekPenalty = mode == 0;
            Fixture::seekQuerySupported = mode != 1;
            Fixture::failRead = mode == 2 ? (Fixture::records > 4096 ? 2 : 1) : 0;
            BlockingQueue<CItem*> queue;
            Fixture::cancelQueue = mode == 3 ? &queue : nullptr;
            CItem root(L"fixture");
            FinderNtfsContext context;
            bool loaded = false;
            try { loaded = context.LoadRoot(&root, &queue); }
            catch (const std::exception&) { Require(mode == 3, "Only cancellation throws"); }
            Require(loaded == (mode < 2) && context.IsLoaded() == loaded, "Failure/cancellation cannot publish a partial index");
            if (loaded) Verify(context, Fixture::records);
            std::cout << std::format("PASS fallback/failure mode={}\n", mode) << std::flush;
            Fixture::cancelQueue = nullptr;
        }
        TestPauseOrCancel(false);
        TestPauseOrCancel(true);
        std::filesystem::remove(Fixture::path);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
}
