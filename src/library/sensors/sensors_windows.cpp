// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

/**
 * @file sensors_windows.cpp
 * @brief (HW_SENSORS: AUTO on Windows) Hardware sensors read from the Windows API.
 */

#include "library/sensors/sensors_windows.h"

#include <cmath>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <powerbase.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

#include "library/log.h"
#include "library/platform.h"
#include "library/sensors/sensors_python.h"
#endif

namespace sensors_windows {

std::vector<std::vector<uint8_t>> smbios_structures(const std::vector<uint8_t> &table)
{
    constexpr uint8_t END_OF_TABLE_TYPE = 127;
    std::vector<std::vector<uint8_t>> structures;
    size_t start = 0;
    while (start + 4 <= table.size()) {
        const size_t formatted_length = table[start + 1];
        if (formatted_length < 4 || start + formatted_length > table.size()) {
            break;
        }
        // The strings follow the formatted part: the structure ends with two NUL bytes
        size_t end = start + formatted_length;
        while (end + 1 < table.size() && (table[end] != 0 || table[end + 1] != 0)) {
            end++;
        }
        if (end + 1 >= table.size()) {
            break;  // Truncated structure
        }
        end += 2;
        structures.emplace_back(table.begin() + static_cast<std::ptrdiff_t>(start),
                                table.begin() + static_cast<std::ptrdiff_t>(end));
        if (table[start] == END_OF_TABLE_TYPE) {
            break;
        }
        start = end;
    }
    return structures;
}

void LoadAverage::add_sample(double queue_length, double elapsed_s)
{
    // Same averages as psutil, which samples every 5 s with the factors exp(-5/60), exp(-5/300), exp(-5/900): here the
    // factors follow the time elapsed since the previous sample
    constexpr std::array<double, 3> periods = {60.0, 300.0, 900.0};
    for (size_t i = 0; i < averages.size(); i++) {
        const double factor = std::exp(-std::max(elapsed_s, 0.0) / periods[i]);
        averages[i] = averages[i] * factor + queue_length * (1.0 - factor);
    }
}

#ifdef _WIN32

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

double round1(double v)
{
    return std::round(v * 10.0) / 10.0;
}

std::string normalize_spaces(const std::string &text)
{
    std::string result;
    for (const char c: text) {
        if (c == ' ' || c == '\t') {
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
            }
        } else {
            result += c;
        }
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

/** @brief A performance counter (PDH), by its English path: the localized names differ with the Windows language. */
class PdhCounter
{
public:
    explicit PdhCounter(const wchar_t *path)
    {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            return;
        }
        if (PdhAddEnglishCounterW(query, path, 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
        }
    }

    ~PdhCounter()
    {
        if (query) {
            PdhCloseQuery(query);
        }
    }

    PdhCounter(const PdhCounter &) = delete;
    PdhCounter &operator=(const PdhCounter &) = delete;

    /** @brief Current value of the counter (instantaneous counters only), std::nullopt if not available. */
    std::optional<double> value()
    {
        std::lock_guard lock(mutex);
        PDH_FMT_COUNTERVALUE result{};
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS ||
            PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &result) != ERROR_SUCCESS) {
            return std::nullopt;
        }
        return result.doubleValue;
    }

private:
    std::mutex mutex;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
};

struct CpuTimes
{
    double total;
    double busy;
};

double to_seconds(const FILETIME &time)
{
    // FILETIME: 100 ns units
    return static_cast<double>(static_cast<uint64_t>(time.dwHighDateTime) << 32 | time.dwLowDateTime) / 1e7;
}

std::optional<CpuTimes> read_cpu_times()
{
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return std::nullopt;
    }
    // The kernel time includes the idle time
    const double total = to_seconds(kernel) + to_seconds(user);
    return CpuTimes{total, total - to_seconds(idle)};
}

std::optional<MEMORYSTATUSEX> memory_status()
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return std::nullopt;
    }
    return status;
}

struct DiskUsage
{
    int64_t used;
    int64_t free;
    double percent;
};

std::optional<DiskUsage> system_disk_usage()
{
    // Root of the drive where Windows is installed, for example "C:\"
    wchar_t windows_directory[MAX_PATH] = {};
    const UINT length = GetSystemWindowsDirectoryW(windows_directory, MAX_PATH);
    const std::wstring root = length >= 3 ? std::wstring(windows_directory, 3) : L"C:\\";
    ULARGE_INTEGER available{}, total{}, total_free{};
    if (!GetDiskFreeSpaceExW(root.c_str(), &available, &total, &total_free)) {
        return std::nullopt;
    }
    // Same computation as psutil.disk_usage() on Windows
    const auto used = static_cast<int64_t>(total.QuadPart - total_free.QuadPart);
    const auto total_bytes = static_cast<double>(total.QuadPart);
    const double percent = total_bytes > 0 ? round1(100.0 * static_cast<double>(used) / total_bytes) : 0.0;
    return DiskUsage{used, static_cast<int64_t>(available.QuadPart), percent};
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Cpu

double Cpu::percentage(std::optional<double> interval)
{
    // Same behavior as psutil.cpu_percent(interval)
    std::lock_guard lock(mutex);
    if (interval && *interval > 0) {
        const auto before = read_cpu_times();
        std::this_thread::sleep_for(std::chrono::duration<double>(*interval));
        if (before) {
            last_total = before->total;
            last_busy = before->busy;
            has_last_times = true;
        }
    }
    const auto now = read_cpu_times();
    if (!now) {
        return NaN;
    }
    double result = 0.0;
    if (has_last_times) {
        const double total_delta = now->total - last_total;
        const double busy_delta = now->busy - last_busy;
        if (total_delta > 0) {
            result = std::clamp(round1(busy_delta / total_delta * 100.0), 0.0, 100.0);
        }
    }
    last_total = now->total;
    last_busy = now->busy;
    has_last_times = true;
    return result;
}

double Cpu::frequency()
{
    // Average of the current frequency of all processors, like psutil.cpu_freq()
    struct ProcessorPowerInformation  // PROCESSOR_POWER_INFORMATION, not declared by all SDKs
    {
        ULONG Number;
        ULONG MaxMhz;
        ULONG CurrentMhz;
        ULONG MhzLimit;
        ULONG MaxIdleState;
        ULONG CurrentIdleState;
    };
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0) {
        return NaN;
    }
    std::vector<ProcessorPowerInformation> info(count);
    const auto size = static_cast<ULONG>(info.size() * sizeof(ProcessorPowerInformation));
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, info.data(), size) != 0) {
        return NaN;
    }
    double sum = 0;
    for (const auto &processor: info) {
        sum += processor.CurrentMhz;
    }
    return sum / static_cast<double>(info.size());
}

std::string Cpu::model()
{
    wchar_t name[256] = {};
    DWORD size = sizeof(name);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &size) != ERROR_SUCCESS) {
        return {};
    }
    return normalize_spaces(platform::to_utf8(name));
}

std::array<double, 3> Cpu::load()
{
    static PdhCounter queue_length(L"\\System\\Processor Queue Length");
    const auto sample = queue_length.value();
    if (!sample) {
        return {NaN, NaN, NaN};
    }
    std::lock_guard lock(mutex);
    const double now = platform::uptime_seconds();
    // The first sample counts like psutil's first 5 s sample
    load_average.add_sample(*sample, last_load_sample ? now - *last_load_sample : 5.0);
    last_load_sample = now;
    return load_average.values();
}

double Cpu::temperature()
{
    return NaN;
}

double Cpu::fan_percent(const std::optional<std::string> &)
{
    return NaN;
}

// ---------------------------------------------------------------------------------------------------------------------
// Memory

double Memory::swap_percent()
{
    static PdhCounter paging_file_usage(L"\\Paging File(_Total)\\% Usage");
    const auto usage = paging_file_usage.value();
    return usage ? round1(*usage) : 0.0;  // No paging file: no swap used
}

double Memory::virtual_percent()
{
    const auto status = memory_status();
    if (!status || status->ullTotalPhys == 0) {
        return NaN;
    }
    const auto total = static_cast<double>(status->ullTotalPhys);
    return round1((total - static_cast<double>(status->ullAvailPhys)) / total * 100);
}

int64_t Memory::virtual_used()
{
    // Total - available, like on Linux
    const auto status = memory_status();
    return status ? static_cast<int64_t>(status->ullTotalPhys - status->ullAvailPhys) : -1;
}

int64_t Memory::virtual_free()
{
    const auto status = memory_status();
    return status ? static_cast<int64_t>(status->ullAvailPhys) : -1;
}

std::string Memory::model()
{
    // Raw SMBIOS table ("RSMB" provider), readable without administrator rights: a RawSMBIOSData header of 8 bytes
    // (version, then the length of the table), then the table
    constexpr DWORD RSMB = 0x52534D42;
    constexpr size_t HEADER_SIZE = 8;
    const UINT size = GetSystemFirmwareTable(RSMB, 0, nullptr, 0);
    if (size <= HEADER_SIZE) {
        return {};
    }
    std::vector<uint8_t> data(size);
    if (GetSystemFirmwareTable(RSMB, 0, data.data(), size) != size) {
        return {};
    }
    const uint32_t length = data[4] | data[5] << 8 | data[6] << 16 | static_cast<uint32_t>(data[7]) << 24;
    const size_t end = std::min<size_t>(size, HEADER_SIZE + length);
    const std::vector<uint8_t> table(data.begin() + HEADER_SIZE, data.begin() + static_cast<std::ptrdiff_t>(end));
    for (const auto &structure: smbios_structures(table)) {
        if (const auto model = sensors_python::memory_device_model(structure)) {
            return *model;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------------
// Disk

double Disk::disk_usage_percent()
{
    const auto usage = system_disk_usage();
    return usage ? usage->percent : NaN;
}

int64_t Disk::disk_used()
{
    const auto usage = system_disk_usage();
    return usage ? usage->used : -1;
}

int64_t Disk::disk_free()
{
    const auto usage = system_disk_usage();
    return usage ? usage->free : -1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Net

sensors::Net::Stats Net::stats(const std::string &if_name, std::optional<double> interval)
{
    Stats result{0, 0, 0, 0};
    if (if_name.empty()) {
        return result;
    }

    // Counters of the interface, found by its name ("alias"), like psutil.net_io_counters(pernic=True)
    MIB_IF_TABLE2 *table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) {
        return {-1, -1, -1, -1};
    }
    std::optional<Counters> pnic_after;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IF_ROW2 &row = table->Table[i];
        if (platform::to_utf8(row.Alias) == if_name) {
            pnic_after = Counters{static_cast<int64_t>(row.OutOctets), static_cast<int64_t>(row.InOctets)};
            break;
        }
    }
    FreeMibTable(table);

    if (!pnic_after) {
        logger.warning("Network interface '{}' not found. Check names in config.json.", if_name);
        return result;
    }

    std::lock_guard lock(mutex);
    const auto before = pnic_before.find(if_name);
    // Interface might not be in pnic_before for now
    if (before != pnic_before.end()) {
        if (interval && *interval > 0) {
            result.upload_rate = (pnic_after->bytes_sent - before->second.bytes_sent) / *interval;
            result.download_rate = (pnic_after->bytes_recv - before->second.bytes_recv) / *interval;
        }
        result.uploaded = pnic_after->bytes_sent;
        result.downloaded = pnic_after->bytes_recv;
    }
    pnic_before[if_name] = *pnic_after;
    return result;
}

sensors::Sensors create()
{
    // GPU: NVIDIA through NVML (nvml.dll), as on Linux; the AMD GPUs are not supported on Windows yet
    return {std::make_unique<Cpu>(), std::make_unique<sensors_python::Gpu>(), std::make_unique<Memory>(),
            std::make_unique<Disk>(), std::make_unique<Net>()};
}

#endif

}  // namespace sensors_windows
