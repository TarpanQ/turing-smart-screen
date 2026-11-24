// SPDX-License-Identifier: GPL-3.0-or-later
//
// turing-smart-screen - C++ port of turing-smart-screen-python, a system monitor and library for USB-C displays
// https://github.com/mathoudebine/turing-smart-screen-python/
//
// Copyright (C) 2021 Matthieu Houdebine (mathoudebine)
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
 * @file sensors_python.cpp
 * @brief Hardware sensors read from Linux /proc and /sys, and NVIDIA NVML.
 */

#include "library/sensors/sensors_python.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>

#include <chrono>
#include <limits>
#include <thread>

#include <dlfcn.h>
#include <sys/statvfs.h>

#include "library/log.h"

namespace fs = std::filesystem;

namespace sensors_python {

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

std::optional<std::string> read_first_line(const fs::path &path)
{
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return std::nullopt;
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.pop_back();
    }
    return line;
}

std::optional<long long> read_int(const fs::path &path)
{
    const auto line = read_first_line(path);
    if (!line) {
        return std::nullopt;
    }
    try {
        size_t pos;
        const long long value = std::stoll(*line, &pos);
        return value;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Names read from the system: runs of spaces collapsed to one, no leading or trailing space
std::string normalize_spaces(const std::string &text)
{
    std::istringstream words(text);
    std::string word, result;
    while (words >> word) {
        if (!result.empty()) {
            result += ' ';
        }
        result += word;
    }
    return result;
}

// Rounding to 1 decimal like psutil percentages
double round1(double v)
{
    return std::round(v * 10.0) / 10.0;
}

// Sorted list of the sysfs files matching <dir>/hwmon*/<prefix>*_* (or <dir>/hwmon*/device/<prefix>*_*), returned
// as the "base" path without the "_xxx" suffix (like psutil does)
std::vector<std::string> hwmon_bases(const std::string &prefix)
{
    std::set<std::string> bases;
    std::error_code ec;
    for (const std::string sub: {"", "device"}) {
        for (const auto &hwmon: fs::directory_iterator("/sys/class/hwmon", ec)) {
            const fs::path dir = sub.empty() ? hwmon.path() : hwmon.path() / sub;
            std::error_code ec2;
            for (const auto &entry: fs::directory_iterator(dir, ec2)) {
                const std::string name = entry.path().filename().string();
                if (name.rfind(prefix, 0) == 0 && name.find('_') != std::string::npos) {
                    const std::string full = entry.path().string();
                    bases.insert(full.substr(0, full.rfind('_')));
                }
            }
        }
        if (!bases.empty()) {
            break;  // CentOS has an intermediate /device directory, only used if nothing is found without it
        }
    }
    return {bases.begin(), bases.end()};
}

// psutil sensors_temperatures(): hwmon sensor name -> temperatures (°C), in sysfs order
std::vector<std::pair<std::string, std::vector<double>>> sensors_temperatures()
{
    std::vector<std::pair<std::string, std::vector<double>>> result;
    auto add = [&](const std::string &name, double value) {
        auto it = std::find_if(result.begin(), result.end(), [&](const auto &p) { return p.first == name; });
        if (it == result.end()) {
            result.push_back({name, {value}});
        } else {
            it->second.push_back(value);
        }
    };

    for (const auto &base: hwmon_bases("temp")) {
        const auto current = read_int(base + "_input");
        if (!current) {
            continue;
        }
        const auto unit_name = read_first_line(fs::path(base).parent_path() / "name");
        add(unit_name.value_or(""), *current / 1000.0);
    }

    // Thermal zones
    std::error_code ec;
    std::vector<fs::path> zones;
    for (const auto &entry: fs::directory_iterator("/sys/class/thermal", ec)) {
        if (entry.path().filename().string().rfind("thermal_zone", 0) == 0) {
            zones.push_back(entry.path());
        }
    }
    std::sort(zones.begin(), zones.end());
    for (const auto &zone: zones) {
        const auto current = read_int(zone / "temp");
        const auto type = read_first_line(zone / "type");
        if (current && type) {
            add(*type, *current / 1000.0);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// NVIDIA NVML library, loaded at runtime (the program also works without NVIDIA driver)

using nvmlReturn_t = int;
using nvmlDevice_t = void *;

struct nvmlUtilization_t
{
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t
{
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

constexpr nvmlReturn_t NVML_SUCCESS = 0;
constexpr int NVML_TEMPERATURE_GPU = 0;
constexpr int NVML_CLOCK_GRAPHICS = 0;
constexpr unsigned int NVML_DEVICE_NAME_BUFFER_SIZE = 64;

class Nvml
{
public:
    static Nvml &instance()
    {
        static Nvml nvml;
        return nvml;
    }

    bool available() const { return initialized; }

    std::vector<nvmlDevice_t> devices()
    {
        std::vector<nvmlDevice_t> result;
        unsigned int count = 0;
        if (!initialized || DeviceGetCount(&count) != NVML_SUCCESS) {
            return result;
        }
        for (unsigned int i = 0; i < count; i++) {
            nvmlDevice_t device;
            if (DeviceGetHandleByIndex(i, &device) == NVML_SUCCESS) {
                result.push_back(device);
            }
        }
        return result;
    }

    nvmlReturn_t (*DeviceGetCount)(unsigned int *) = nullptr;
    nvmlReturn_t (*DeviceGetHandleByIndex)(unsigned int, nvmlDevice_t *) = nullptr;
    nvmlReturn_t (*DeviceGetUtilizationRates)(nvmlDevice_t, nvmlUtilization_t *) = nullptr;
    nvmlReturn_t (*DeviceGetMemoryInfo)(nvmlDevice_t, nvmlMemory_t *) = nullptr;
    nvmlReturn_t (*DeviceGetTemperature)(nvmlDevice_t, int, unsigned int *) = nullptr;
    nvmlReturn_t (*DeviceGetClockInfo)(nvmlDevice_t, int, unsigned int *) = nullptr;
    nvmlReturn_t (*DeviceGetFanSpeed)(nvmlDevice_t, unsigned int *) = nullptr;
    // Optional: only used for the GPU model, so it is not required to use the other functions
    nvmlReturn_t (*DeviceGetName)(nvmlDevice_t, char *, unsigned int) = nullptr;

private:
    Nvml()
    {
        handle = dlopen("libnvidia-ml.so.1", RTLD_NOW);
        if (!handle) {
            return;
        }
        auto init = reinterpret_cast<nvmlReturn_t (*)()>(dlsym(handle, "nvmlInit_v2"));
        DeviceGetCount = reinterpret_cast<decltype(DeviceGetCount)>(dlsym(handle, "nvmlDeviceGetCount_v2"));
        DeviceGetHandleByIndex =
            reinterpret_cast<decltype(DeviceGetHandleByIndex)>(dlsym(handle, "nvmlDeviceGetHandleByIndex_v2"));
        DeviceGetUtilizationRates =
            reinterpret_cast<decltype(DeviceGetUtilizationRates)>(dlsym(handle, "nvmlDeviceGetUtilizationRates"));
        DeviceGetMemoryInfo = reinterpret_cast<decltype(DeviceGetMemoryInfo)>(dlsym(handle, "nvmlDeviceGetMemoryInfo"));
        DeviceGetTemperature =
            reinterpret_cast<decltype(DeviceGetTemperature)>(dlsym(handle, "nvmlDeviceGetTemperature"));
        DeviceGetClockInfo = reinterpret_cast<decltype(DeviceGetClockInfo)>(dlsym(handle, "nvmlDeviceGetClockInfo"));
        DeviceGetFanSpeed = reinterpret_cast<decltype(DeviceGetFanSpeed)>(dlsym(handle, "nvmlDeviceGetFanSpeed"));
        DeviceGetName = reinterpret_cast<decltype(DeviceGetName)>(dlsym(handle, "nvmlDeviceGetName"));
        initialized = init && DeviceGetCount && DeviceGetHandleByIndex && DeviceGetUtilizationRates &&
                      DeviceGetMemoryInfo && DeviceGetTemperature && DeviceGetClockInfo && DeviceGetFanSpeed &&
                      init() == NVML_SUCCESS;
    }

    void *handle = nullptr;
    bool initialized = false;
};

// Average of the values returned successfully for each device, NaN if none
template<typename F>
double nvml_average(F &&get_value)
{
    double sum = 0;
    int count = 0;
    for (nvmlDevice_t device: Nvml::instance().devices()) {
        double value;
        if (get_value(device, value)) {
            sum += value;
            count++;
        }
    }
    return count > 0 ? sum / count : NaN;
}

// ---------------------------------------------------------------------------------------------------------------------
// AMD GPU through the amdgpu driver sysfs interface

// Device directory (/sys/class/drm/cardX/device) of the first AMD GPU, if any
std::optional<fs::path> amd_gpu_device()
{
    std::error_code ec;
    std::vector<fs::path> cards;
    for (const auto &entry: fs::directory_iterator("/sys/class/drm", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("card", 0) == 0 && name.find('-') == std::string::npos) {
            cards.push_back(entry.path());
        }
    }
    std::sort(cards.begin(), cards.end());
    for (const auto &card: cards) {
        const fs::path device = card / "device";
        if (read_first_line(device / "vendor") == "0x1002" && fs::exists(device / "gpu_busy_percent", ec)) {
            return device;
        }
    }
    return std::nullopt;
}

// First hwmon directory of a device
std::optional<fs::path> device_hwmon(const fs::path &device)
{
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (const auto &entry: fs::directory_iterator(device / "hwmon", ec)) {
        dirs.push_back(entry.path());
    }
    std::sort(dirs.begin(), dirs.end());
    if (dirs.empty()) {
        return std::nullopt;
    }
    return dirs.front();
}

// GPU fan from hwmon fans
std::optional<double> gpu_fan_from_hwmon()
{
    for (const auto &[name, entries]: sensors_fans()) {
        for (const auto &entry: entries) {
            if (lower(entry.label).find("gpu") != std::string::npos || lower(name).find("gpu") != std::string::npos) {
                return entry.percent;
            }
        }
    }
    return std::nullopt;
}

struct CpuTimes
{
    double total;
    double busy;
};

std::optional<CpuTimes> read_cpu_times()
{
    std::ifstream file("/proc/stat");
    std::string cpu;
    file >> cpu;
    if (cpu != "cpu") {
        return std::nullopt;
    }
    // user nice system idle iowait irq softirq steal guest guest_nice
    double fields[10] = {};
    for (double &field: fields) {
        if (!(file >> field)) {
            field = 0;
            file.clear();
        }
    }
    double total = 0;
    for (double field: fields) {
        total += field;
    }
    // Like psutil: guest times are already counted in user/nice times
    total -= fields[8] + fields[9];
    const double busy = total - fields[3] - fields[4];
    return CpuTimes{total, busy};
}

std::map<std::string, long long> read_meminfo()
{
    std::map<std::string, long long> result;
    std::ifstream file("/proc/meminfo");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        long long value;
        if (iss >> key >> value) {
            key.pop_back();  // ':'
            result[key] = value * 1024;  // kB -> bytes
        }
    }
    return result;
}

struct DiskUsage
{
    int64_t used;
    int64_t free;
    double percent;
};

std::optional<DiskUsage> disk_usage(const char *path)
{
    struct statvfs st{};
    if (statvfs(path, &st) != 0) {
        return std::nullopt;
    }
    // Same computation as psutil.disk_usage()
    const int64_t avail_to_user = static_cast<int64_t>(st.f_bavail) * st.f_frsize;
    const int64_t used = static_cast<int64_t>(st.f_blocks - st.f_bfree) * st.f_frsize;
    const int64_t total_user = used + avail_to_user;
    return DiskUsage{used, avail_to_user, total_user > 0 ? round1(100.0 * used / total_user) : 0.0};
}

}  // namespace

std::vector<std::pair<std::string, std::vector<FanInfo>>> sensors_fans()
{
    std::vector<std::pair<std::string, std::vector<FanInfo>>> ret;

    for (const auto &base: hwmon_bases("fan")) {
        const auto current_rpm = read_int(base + "_input");
        if (!current_rpm) {
            continue;
        }

        long long max_rpm = read_int(base + "_max").value_or(0);
        if (!max_rpm) {
            // Real maximum speed not found
            if (*current_rpm > 2200) {
                max_rpm = 3000;  // AIO Pumps are usualy 3000 RPM
            } else if (*current_rpm > 1500) {
                max_rpm = 2200;  // High speed fans are usualy 2200 RPM
            } else {
                max_rpm = 1500;  // Approximated: max fan speed is 1500 RPM
            }
        }
        // Approximated: min fan speed is 0 RPM
        const long long min_rpm = read_int(base + "_min").value_or(0);
        if (max_rpm == min_rpm) {
            continue;
        }
        const int percent = static_cast<int>(static_cast<double>(*current_rpm - min_rpm) / (max_rpm - min_rpm) * 100);

        const std::string unit_name = read_first_line(fs::path(base).parent_path() / "name").value_or("");
        const std::string label = read_first_line(base + "_label").value_or(fs::path(base).filename().string());

        auto it = std::find_if(ret.begin(), ret.end(), [&](const auto &p) { return p.first == unit_name; });
        if (it == ret.end()) {
            ret.push_back({unit_name, {}});
            it = ret.end() - 1;
        }
        it->second.push_back({label, static_cast<int>(*current_rpm), percent});
    }
    return ret;
}

/** @brief True if a fan label or a sensor name corresponds to the CPU fan. */
bool is_cpu_fan(const std::string &label)
{
    const std::string l = lower(label);
    return l.find("cpu") != std::string::npos || l.find("proc") != std::string::npos;
}

std::optional<std::string> memory_device_model(const std::vector<uint8_t> &raw)
{
    // Formatted part of a Memory Device structure (type 17): the size is a 16-bit value at 0x0C (0 if the slot is
    // empty), the manufacturer and the part number are string indexes at 0x17 and 0x1A
    constexpr uint8_t MEMORY_DEVICE_TYPE = 17;
    constexpr size_t SIZE_OFFSET = 0x0C, MANUFACTURER_OFFSET = 0x17, PART_NUMBER_OFFSET = 0x1A;
    if (raw.size() < 2 || raw[0] != MEMORY_DEVICE_TYPE) {
        return std::nullopt;
    }
    const size_t formatted_length = raw[1];
    if (formatted_length <= PART_NUMBER_OFFSET || raw.size() < formatted_length) {
        return std::nullopt;
    }
    if ((raw[SIZE_OFFSET] | (raw[SIZE_OFFSET + 1] << 8)) == 0) {
        return std::nullopt;
    }

    // The strings follow the formatted part, each one ends with a NUL byte, and the list ends with an empty string
    std::vector<std::string> strings;
    std::string current;
    for (size_t i = formatted_length; i < raw.size(); i++) {
        if (raw[i] != 0) {
            current += static_cast<char>(raw[i]);
        } else if (current.empty()) {
            break;
        } else {
            strings.push_back(current);
            current.clear();
        }
    }
    // String indexes start at 1, 0 means no string
    const auto string_at = [&strings](uint8_t index) -> std::string {
        return index >= 1 && index <= strings.size() ? strings[index - 1] : std::string();
    };

    std::string model;
    for (const std::string &part: {string_at(raw[MANUFACTURER_OFFSET]), string_at(raw[PART_NUMBER_OFFSET])}) {
        const std::string name = normalize_spaces(part);
        if (name.empty() || name == "Not Specified" || name == "Unknown") {
            continue;
        }
        if (!model.empty()) {
            model += ' ';
        }
        model += name;
    }
    if (model.empty()) {
        return std::nullopt;
    }
    return model;
}

std::string pci_device_name(std::istream &pci_ids, uint16_t vendor, uint16_t device)
{
    // pci.ids format: "vvvv  Vendor name" lines, each followed by its "\tdddd  Device name" lines
    const std::string vendor_id = std::format("{:04x}", vendor);
    const std::string device_id = std::format("{:04x}", device);
    bool in_vendor = false;
    std::string line;
    while (std::getline(pci_ids, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line[0] != '\t') {
            in_vendor = line.rfind(vendor_id, 0) == 0;
            continue;
        }
        // Subsystem lines start with two tabs, and never match the device id
        if (in_vendor && line.size() > 6 && line.compare(1, device_id.size(), device_id) == 0) {
            return normalize_spaces(line.substr(6));
        }
    }
    return {};
}

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
    // Average of the current frequency of all CPUs, like psutil.cpu_freq()
    std::error_code ec;
    double sum = 0;
    int count = 0;
    for (const auto &entry: fs::directory_iterator("/sys/devices/system/cpu/cpufreq", ec)) {
        if (entry.path().filename().string().rfind("policy", 0) != 0) {
            continue;
        }
        if (const auto khz = read_int(entry.path() / "scaling_cur_freq")) {
            sum += *khz / 1000.0;
            count++;
        }
    }
    if (count > 0) {
        return sum / count;
    }

    // Fallback: /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("cpu MHz", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                try {
                    sum += std::stod(line.substr(colon + 1));
                    count++;
                } catch (const std::exception &) {
                }
            }
        }
    }
    return count > 0 ? sum / count : NaN;
}

std::string Cpu::model()
{
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            return colon == std::string::npos ? std::string() : normalize_spaces(line.substr(colon + 1));
        }
    }
    return {};
}

std::array<double, 3> Cpu::load()
{
    std::ifstream file("/proc/loadavg");
    std::array<double, 3> result{};
    if (!(file >> result[0] >> result[1] >> result[2])) {
        return {NaN, NaN, NaN};
    }
    return result;
}

double Cpu::temperature()
{
    const auto temps = sensors_temperatures();
    // Intel CPU, AMD CPU, ARM CPU, AMD CPU with zenpower (k10temp is in blacklist)
    for (const char *name: {"coretemp", "k10temp", "cpu_thermal", "zenpower"}) {
        const auto it = std::find_if(temps.begin(), temps.end(), [&](const auto &p) { return p.first == name; });
        if (it != temps.end() && !it->second.empty()) {
            return it->second.front();
        }
    }
    return NaN;
}

double Cpu::fan_percent(const std::optional<std::string> &fan_name)
{
    for (const auto &[name, entries]: sensors_fans()) {
        for (const auto &entry: entries) {
            if (fan_name && *fan_name == name + "/" + entry.label) {
                // Manually selected fan
                return entry.percent;
            } else if (is_cpu_fan(entry.label) || is_cpu_fan(name)) {
                // Auto-detected fan
                return entry.percent;
            }
        }
    }
    return NaN;
}

// ---------------------------------------------------------------------------------------------------------------------
// Gpu

sensors::Gpu::Stats Gpu::stats()
{
    switch (detected_gpu) {
        case GpuType::AMD: return amd.stats();
        case GpuType::NVIDIA: return nvidia.stats();
        default: return {NaN, NaN, NaN, NaN, NaN};
    }
}

int Gpu::fps()
{
    switch (detected_gpu) {
        case GpuType::AMD: return amd.fps();
        case GpuType::NVIDIA: return nvidia.fps();
        default: return -1;
    }
}

double Gpu::fan_percent()
{
    switch (detected_gpu) {
        case GpuType::AMD: return amd.fan_percent();
        case GpuType::NVIDIA: return nvidia.fan_percent();
        default: return NaN;
    }
}

double Gpu::frequency()
{
    switch (detected_gpu) {
        case GpuType::AMD: return amd.frequency();
        case GpuType::NVIDIA: return nvidia.frequency();
        default: return NaN;
    }
}

std::string Gpu::model()
{
    switch (detected_gpu) {
        case GpuType::AMD: return amd.model();
        case GpuType::NVIDIA: return nvidia.model();
        default: return {};
    }
}

bool Gpu::is_available()
{
    // Always use Nvidia GPU if available
    if (nvidia.is_available()) {
        logger.info("Detected Nvidia GPU(s)");
        detected_gpu = GpuType::NVIDIA;
    } else if (amd.is_available()) {
        // Otherwise, use the AMD GPU / APU if available
        logger.info("Detected AMD GPU(s)");
        detected_gpu = GpuType::AMD;
    } else {
        logger.warning("No supported GPU found");
        detected_gpu = GpuType::UNSUPPORTED;
    }
    return detected_gpu != GpuType::UNSUPPORTED;
}

// ---------------------------------------------------------------------------------------------------------------------
// GpuNvidia

sensors::Gpu::Stats GpuNvidia::stats()
{
    // Unlike other sensors, Nvidia GPU pulls in all the stats at once. Values are averaged over all GPUs.
    Nvml &nvml = Nvml::instance();
    const double memory_used_mb = nvml_average([&](nvmlDevice_t d, double &v) {
        nvmlMemory_t mem;
        if (nvml.DeviceGetMemoryInfo(d, &mem) != NVML_SUCCESS) return false;
        v = mem.used / 1024.0 / 1024.0;
        return true;
    });
    const double memory_total_mb = nvml_average([&](nvmlDevice_t d, double &v) {
        nvmlMemory_t mem;
        if (nvml.DeviceGetMemoryInfo(d, &mem) != NVML_SUCCESS) return false;
        v = mem.total / 1024.0 / 1024.0;
        return true;
    });
    const double memory_percentage = (memory_used_mb / memory_total_mb) * 100;
    const double load = nvml_average([&](nvmlDevice_t d, double &v) {
        nvmlUtilization_t util;
        if (nvml.DeviceGetUtilizationRates(d, &util) != NVML_SUCCESS) return false;
        v = util.gpu;
        return true;
    });
    const double temperature = nvml_average([&](nvmlDevice_t d, double &v) {
        unsigned int temp;
        if (nvml.DeviceGetTemperature(d, NVML_TEMPERATURE_GPU, &temp) != NVML_SUCCESS) return false;
        v = temp;
        return true;
    });
    return {load, memory_percentage, memory_used_mb, memory_total_mb, temperature};
}

int GpuNvidia::fps()
{
    // Not supported
    return -1;
}

double GpuNvidia::fan_percent()
{
    if (const auto fan = gpu_fan_from_hwmon()) {
        return *fan;
    }
    // Nvidia GPU fans are usually not exposed in hwmon: use NVML
    Nvml &nvml = Nvml::instance();
    return nvml_average([&](nvmlDevice_t d, double &v) {
        unsigned int speed;
        if (nvml.DeviceGetFanSpeed(d, &speed) != NVML_SUCCESS) return false;
        v = speed;
        return true;
    });
}

std::string GpuNvidia::model()
{
    Nvml &nvml = Nvml::instance();
    const auto devices = nvml.devices();
    if (devices.empty() || !nvml.DeviceGetName) {
        return {};
    }
    char name[NVML_DEVICE_NAME_BUFFER_SIZE] = {};
    if (nvml.DeviceGetName(devices.front(), name, NVML_DEVICE_NAME_BUFFER_SIZE) != NVML_SUCCESS) {
        return {};
    }
    return normalize_spaces(name);
}

double GpuNvidia::frequency()
{
    Nvml &nvml = Nvml::instance();
    return nvml_average([&](nvmlDevice_t d, double &v) {
        unsigned int mhz;
        if (nvml.DeviceGetClockInfo(d, NVML_CLOCK_GRAPHICS, &mhz) != NVML_SUCCESS) return false;
        v = mhz;
        return true;
    });
}

bool GpuNvidia::is_available()
{
    return Nvml::instance().available() && !Nvml::instance().devices().empty();
}

// ---------------------------------------------------------------------------------------------------------------------
// GpuAmd

sensors::Gpu::Stats GpuAmd::stats()
{
    const auto device = amd_gpu_device();
    if (!device) {
        return {NaN, NaN, NaN, NaN, NaN};
    }

    const auto used_bytes = read_int(*device / "mem_info_vram_used");
    const auto total_bytes = read_int(*device / "mem_info_vram_total");
    const double memory_used = used_bytes ? *used_bytes / 1024.0 / 1024.0 : NaN;
    const double memory_total = total_bytes ? *total_bytes / 1024.0 / 1024.0 : NaN;
    const double memory_percentage =
        used_bytes && total_bytes && *total_bytes > 0 ? static_cast<double>(*used_bytes) / *total_bytes * 100 : NaN;

    const auto busy = read_int(*device / "gpu_busy_percent");
    const double load = busy ? static_cast<double>(*busy) : NaN;

    double temperature = NaN;
    if (const auto hwmon = device_hwmon(*device)) {
        if (const auto temp = read_int(*hwmon / "temp1_input")) {
            temperature = *temp / 1000.0;
        }
    }

    return {load, memory_percentage, memory_used, memory_total, temperature};
}

int GpuAmd::fps()
{
    // Not supported
    return -1;
}

double GpuAmd::fan_percent()
{
    if (const auto fan = gpu_fan_from_hwmon()) {
        return *fan;
    }
    return NaN;
}

std::string GpuAmd::model()
{
    const auto device = amd_gpu_device();
    if (!device) {
        return {};
    }
    // The PCI device id of the card, for example "0x744c"
    const auto device_id = read_first_line(*device / "device");
    if (!device_id) {
        return {};
    }
    std::ifstream pci_ids("/usr/share/hwdata/pci.ids");
    if (!pci_ids) {
        pci_ids.open("/usr/share/misc/pci.ids");
    }
    constexpr uint16_t AMD_VENDOR_ID = 0x1002;
    const auto device_code = static_cast<uint16_t>(std::strtoul(device_id->c_str(), nullptr, 16));
    return pci_device_name(pci_ids, AMD_VENDOR_ID, device_code);
}

double GpuAmd::frequency()
{
    const auto device = amd_gpu_device();
    if (!device) {
        return NaN;
    }
    // Shader clock in Hz
    if (const auto hwmon = device_hwmon(*device)) {
        if (const auto hz = read_int(*hwmon / "freq1_input")) {
            return *hz / 1000000.0;
        }
    }
    // Fallback: current DPM level, e.g. "1: 1800Mhz *"
    std::ifstream file(*device / "pp_dpm_sclk");
    std::string line;
    while (std::getline(file, line)) {
        if (line.find('*') != std::string::npos) {
            const auto colon = line.find(':');
            try {
                return std::stod(line.substr(colon + 1));
            } catch (const std::exception &) {
                return NaN;
            }
        }
    }
    return NaN;
}

bool GpuAmd::is_available()
{
    return amd_gpu_device().has_value();
}

// ---------------------------------------------------------------------------------------------------------------------
// Memory

std::string Memory::model()
{
    // One entry per SMBIOS structure: the Memory Device structures (type 17) are named "17-<n>". Root only.
    std::vector<fs::path> entries;
    std::error_code ec;
    for (const auto &entry: fs::directory_iterator("/sys/firmware/dmi/entries", ec)) {
        if (entry.path().filename().string().rfind("17-", 0) == 0) {
            entries.push_back(entry.path());
        }
    }
    std::sort(entries.begin(), entries.end());
    for (const auto &entry: entries) {
        std::ifstream file(entry / "raw", std::ios::binary);
        const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (const auto model = memory_device_model(raw)) {
            return *model;
        }
    }
    return {};
}

double Memory::swap_percent()
{
    const auto mem = read_meminfo();
    if (!mem.contains("SwapTotal") || !mem.contains("SwapFree")) {
        return NaN;
    }
    const double total = mem.at("SwapTotal");
    return total > 0 ? round1((total - mem.at("SwapFree")) / total * 100) : 0.0;
}

double Memory::virtual_percent()
{
    const auto mem = read_meminfo();
    if (!mem.contains("MemTotal") || !mem.contains("MemAvailable")) {
        return NaN;
    }
    const double total = mem.at("MemTotal");
    return total > 0 ? round1((total - mem.at("MemAvailable")) / total * 100) : NaN;
}

int64_t Memory::virtual_used()
{
    // Do not use the "used" memory: it is calculated differently depending on the platform. Use total - available.
    const auto mem = read_meminfo();
    if (!mem.contains("MemTotal") || !mem.contains("MemAvailable")) {
        return -1;
    }
    return mem.at("MemTotal") - mem.at("MemAvailable");
}

int64_t Memory::virtual_free()
{
    // Do not use the "free" memory: it doesn't reflect the actual memory available. Use available instead.
    const auto mem = read_meminfo();
    if (!mem.contains("MemAvailable")) {
        return -1;
    }
    return mem.at("MemAvailable");
}

// ---------------------------------------------------------------------------------------------------------------------
// Disk

double Disk::disk_usage_percent()
{
    const auto usage = disk_usage("/");
    return usage ? usage->percent : NaN;
}

int64_t Disk::disk_used()
{
    const auto usage = disk_usage("/");
    return usage ? usage->used : -1;
}

int64_t Disk::disk_free()
{
    const auto usage = disk_usage("/");
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

    // Get current counters from /proc/net/dev: "  eth0: rx_bytes rx_packets ... (8 rx fields) tx_bytes ..."
    std::ifstream file("/proc/net/dev");
    if (!file) {
        return {-1, -1, -1, -1};
    }
    std::optional<Counters> pnic_after;
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(' '));
        if (name != if_name) {
            continue;
        }
        std::istringstream iss(line.substr(colon + 1));
        int64_t fields[9] = {};
        for (auto &field: fields) {
            iss >> field;
        }
        pnic_after = Counters{fields[8], fields[0]};
        break;
    }

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

/** @brief Create the sensors reading the Linux system files. */
sensors::Sensors create()
{
    return {std::make_unique<Cpu>(), std::make_unique<Gpu>(), std::make_unique<Memory>(), std::make_unique<Disk>(),
            std::make_unique<Net>()};
}

}  // namespace sensors_python
