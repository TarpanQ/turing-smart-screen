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
 * @file sensors.h
 * @brief This file defines all supported hardware in abstract classes and their abstract methods to access sensors. To be overridden by child sensors classes. Unsupported values are NaN (or -1 for integer values).
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sensors {

/** @brief CPU sensors: usage, frequency, load, temperature and fan speed. */
class Cpu
{
public:
    virtual ~Cpu() = default;

    // CPU usage (%). With an interval, the usage is measured during this time (blocking call), otherwise it is
    // measured since the previous call (0.0 is returned the first time)
    /** @brief CPU usage in percent, measured over interval seconds if given. */
    virtual double percentage(std::optional<double> interval) = 0;

    // Current frequency (MHz)
    /** @brief Current CPU frequency, in MHz. */
    virtual double frequency() = 0;

    // 1 / 5 / 15min load average
    /** @brief System load averages over 1, 5 and 15 minutes. */
    virtual std::array<double, 3> load() = 0;

    // Temperature (°C)
    /** @brief CPU temperature, in degrees Celsius. */
    virtual double temperature() = 0;

    // Fan speed (%). fan_name: "<sensor name>/<fan label>" of a manually selected fan, auto-detection otherwise
    /** @brief CPU fan speed in percent, of the fan named fan_name if given. */
    virtual double fan_percent(const std::optional<std::string> &fan_name = std::nullopt) = 0;
};

/** @brief GPU sensors. */
class Gpu
{
public:
    /** @brief Statistics of a GPU. */
    struct Stats
    {
        /** @brief GPU load, in percent. */
        double load;               // %
        /** @brief GPU memory usage, in percent. */
        double memory_percentage;  // %
        /** @brief GPU memory used, in MB. */
        double memory_used_mb;     // MB
        /** @brief GPU memory total, in MB. */
        double memory_total_mb;    // MB
        /** @brief GPU temperature, in degrees Celsius. */
        double temperature;        // °C
    };

    virtual ~Gpu() = default;

    /** @brief Statistics of the GPU. */
    virtual Stats stats() = 0;

    // Frames per second, -1 if not supported
    /** @brief Frames per second of the GPU. */
    virtual int fps() = 0;

    // Fan speed (%)
    /** @brief GPU fan speed in percent. */
    virtual double fan_percent() = 0;

    // Current frequency (MHz)
    /** @brief GPU frequency, in MHz. */
    virtual double frequency() = 0;

    /** @brief True if the GPU sensors are available. */
    virtual bool is_available() = 0;
};

/** @brief Memory sensors: RAM and swap. */
class Memory
{
public:
    virtual ~Memory() = default;

    /** @brief Swap usage, in percent. */
    virtual double swap_percent() = 0;
    /** @brief RAM usage, in percent. */
    virtual double virtual_percent() = 0;
    /** @brief RAM used, in bytes. */
    virtual int64_t virtual_used() = 0;  // In bytes
    /** @brief RAM free, in bytes. */
    virtual int64_t virtual_free() = 0;  // In bytes
};

/** @brief Disk sensors. */
class Disk
{
public:
    virtual ~Disk() = default;

    /** @brief Disk usage, in percent. */
    virtual double disk_usage_percent() = 0;
    /** @brief Disk used, in bytes. */
    virtual int64_t disk_used() = 0;  // In bytes
    /** @brief Disk free, in bytes. */
    virtual int64_t disk_free() = 0;  // In bytes
};

/** @brief Network sensors. */
class Net
{
public:
    /** @brief Network statistics of an interface. */
    struct Stats
    {
        /** @brief Upload rate, in bytes per second. */
        double upload_rate;    // B/s
        /** @brief Total uploaded data, in bytes. */
        int64_t uploaded;      // B
        /** @brief Download rate, in bytes per second. */
        double download_rate;  // B/s
        /** @brief Total downloaded data, in bytes. */
        int64_t downloaded;    // B
    };

    virtual ~Net() = default;

    // Network interface statistics. interval: time (s) since the previous call, to compute the rates
    /** @brief Network statistics of the interface if_name, measured over interval seconds if given. */
    virtual Stats stats(const std::string &if_name, std::optional<double> interval) = 0;
};

/** @brief A set of sensors (selected by HW_SENSORS in config.yaml) */
struct Sensors
{
    /** @brief CPU sensors. */
    std::unique_ptr<Cpu> cpu;
    /** @brief GPU sensors. */
    std::unique_ptr<Gpu> gpu;
    /** @brief Memory sensors. */
    std::unique_ptr<Memory> memory;
    /** @brief Disk sensors. */
    std::unique_ptr<Disk> disk;
    /** @brief Network sensors. */
    std::unique_ptr<Net> net;
};

}  // namespace sensors
