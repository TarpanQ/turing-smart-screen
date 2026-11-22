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
 * @file sensors_python.h
 * @brief (HW_SENSORS: PYTHON, or AUTO) Reads the same information as psutil, GPUtil and pyamdgpuinfo, directly from Linux: /proc, /sys (hwmon, drm), statvfs, and the NVIDIA NVML library (loaded at runtime if present).
 */

#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "library/sensors/sensors.h"

namespace sensors_python {

/** @brief Information about a fan. */
struct FanInfo
{
    /** @brief Label of the fan. */
    std::string label;
    /** @brief Current speed, in RPM. */
    int current;  // RPM
    /** @brief Current speed, in percent of the maximum speed. */
    int percent;
};

/**
 * @brief Hardware fans info (for CPU and other peripherals): hwmon sensor name -> fans, in sysfs order.
 * Inspired of psutil sensors_fans(), adapted to also get fan speed percentage instead of raw value.
 */
std::vector<std::pair<std::string, std::vector<FanInfo>>> sensors_fans();

bool is_cpu_fan(const std::string &label);

/** @brief Kind of GPU. */
enum class GpuType { UNSUPPORTED, AMD, NVIDIA };

/** @brief CPU sensors, read from /proc and /sys. */
class Cpu : public sensors::Cpu
{
public:
    /** @brief CPU usage, in percent. */
    double percentage(std::optional<double> interval) override;
    /** @brief CPU frequency, in MHz. */
    double frequency() override;
    /** @brief System load averages. */
    std::array<double, 3> load() override;
    /** @brief CPU temperature, in degrees Celsius. */
    double temperature() override;
    /** @brief CPU fan speed in percent. */
    double fan_percent(const std::optional<std::string> &fan_name = std::nullopt) override;

private:
    std::mutex mutex;
    bool has_last_times = false;
    double last_total = 0, last_busy = 0;
};

/** @brief NVIDIA GPU sensors, read through NVML. */
class GpuNvidia : public sensors::Gpu
{
public:
    /** @brief Statistics of the NVIDIA GPU. */
    Stats stats() override;
    /** @brief Frames per second of the NVIDIA GPU. */
    int fps() override;
    /** @brief Fan speed of the NVIDIA GPU, in percent. */
    double fan_percent() override;
    /** @brief Frequency of the NVIDIA GPU, in MHz. */
    double frequency() override;
    /** @brief True if an NVIDIA GPU is available. */
    bool is_available() override;
};

/** @brief AMD GPU sensors, read through the amdgpu sysfs interface. */
class GpuAmd : public sensors::Gpu
{
public:
    /** @brief Statistics of the AMD GPU. */
    Stats stats() override;
    /** @brief Frames per second of the AMD GPU. */
    int fps() override;
    /** @brief Fan speed of the AMD GPU, in percent. */
    double fan_percent() override;
    /** @brief Frequency of the AMD GPU, in MHz. */
    double frequency() override;
    /** @brief True if an AMD GPU is available. */
    bool is_available() override;
};

/** @brief GPU sensors, using the NVIDIA or AMD GPU that is detected. */
class Gpu : public sensors::Gpu
{
public:
    /** @brief Statistics of the GPU. */
    Stats stats() override;
    /** @brief Frames per second of the GPU. */
    int fps() override;
    /** @brief Fan speed of the GPU, in percent. */
    double fan_percent() override;
    /** @brief Frequency of the GPU, in MHz. */
    double frequency() override;
    // Detects the GPU: must be called once before the other functions
    /** @brief True if a GPU is available. */
    bool is_available() override;

private:
    GpuType detected_gpu = GpuType::UNSUPPORTED;
    GpuNvidia nvidia;
    GpuAmd amd;
};

/** @brief Memory sensors, read from /proc/meminfo. */
class Memory : public sensors::Memory
{
public:
    /** @brief Swap usage, in percent. */
    double swap_percent() override;
    /** @brief RAM usage, in percent. */
    double virtual_percent() override;
    /** @brief RAM used, in bytes. */
    int64_t virtual_used() override;
    /** @brief RAM free, in bytes. */
    int64_t virtual_free() override;
};

/** @brief Disk sensors. */
class Disk : public sensors::Disk
{
public:
    /** @brief Disk usage, in percent. */
    double disk_usage_percent() override;
    /** @brief Disk used, in bytes. */
    int64_t disk_used() override;
    /** @brief Disk free, in bytes. */
    int64_t disk_free() override;
};

/** @brief Network sensors, read from /sys/class/net. */
class Net : public sensors::Net
{
public:
    /** @brief Network statistics of the interface if_name. */
    Stats stats(const std::string &if_name, std::optional<double> interval) override;

private:
    struct Counters
    {
        int64_t bytes_sent;
        int64_t bytes_recv;
    };

    std::mutex mutex;
    std::map<std::string, Counters> pnic_before;
};

sensors::Sensors create();

}  // namespace sensors_python
