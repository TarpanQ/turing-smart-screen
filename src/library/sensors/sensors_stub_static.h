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
 * @file sensors_stub_static.h
 * @brief This file will use static data instead of real hardware sensors. Useful for theme editor.
 */

#pragma once

#include "library/sensors/sensors.h"

namespace sensors_stub_static {

/** @brief Define here global static values that will be applied to all sensors of the same type */
constexpr double PERCENTAGE_SENSOR_VALUE = 50.0;
/** @brief Fixed temperature returned by the sensors, in degrees Celsius. */
constexpr double TEMPERATURE_SENSOR_VALUE = 67.3;

/** @brief Define other sensors */
constexpr double CPU_FREQ_MHZ = 2400.0;
/** @brief Total size of the fake disk, in GB. */
constexpr int64_t DISK_TOTAL_SIZE_GB = 1000;
/** @brief Total size of the fake memory, in GB. */
constexpr int64_t MEMORY_TOTAL_SIZE_GB = 64;
/** @brief Total size of the fake GPU memory, in GB. */
constexpr int64_t GPU_MEM_TOTAL_SIZE_GB = 32;
/** @brief Fake network speed, in bytes per second. */
constexpr int64_t NETWORK_SPEED_BYTES = 1061000000;
/** @brief Fake GPU frames per second. */
constexpr int GPU_FPS = 120;
/** @brief Fake GPU frequency, in MHz. */
constexpr double GPU_FREQ_MHZ = 1500.0;

/** @brief CPU sensors returning fixed values. */
class Cpu : public sensors::Cpu
{
public:
    /** @brief Fixed CPU usage, in percent. */
    double percentage(std::optional<double> interval) override;
    /** @brief Fixed CPU frequency, in MHz. */
    double frequency() override;
    /** @brief Fixed system load averages. */
    std::array<double, 3> load() override;
    /** @brief Fixed CPU temperature, in degrees Celsius. */
    double temperature() override;
    /** @brief Fixed CPU fan speed, in percent. */
    double fan_percent(const std::optional<std::string> &fan_name = std::nullopt) override;
};

/** @brief GPU sensors returning fixed values. */
class Gpu : public sensors::Gpu
{
public:
    /** @brief Fixed GPU statistics. */
    Stats stats() override;
    /** @brief Fixed frames per second. */
    int fps() override;
    /** @brief Fixed GPU fan speed, in percent. */
    double fan_percent() override;
    /** @brief Fixed GPU frequency, in MHz. */
    double frequency() override;
    /** @brief The fake GPU is always available. */
    bool is_available() override;
};

/** @brief Memory sensors returning fixed values. */
class Memory : public sensors::Memory
{
public:
    /** @brief Fixed swap usage, in percent. */
    double swap_percent() override;
    /** @brief Fixed RAM usage, in percent. */
    double virtual_percent() override;
    /** @brief Fixed RAM used, in bytes. */
    int64_t virtual_used() override;
    /** @brief Fixed RAM free, in bytes. */
    int64_t virtual_free() override;
};

/** @brief Disk sensors returning fixed values. */
class Disk : public sensors::Disk
{
public:
    /** @brief Fixed disk usage, in percent. */
    double disk_usage_percent() override;
    /** @brief Fixed disk used, in bytes. */
    int64_t disk_used() override;
    /** @brief Fixed disk free, in bytes. */
    int64_t disk_free() override;
};

/** @brief Network sensors returning fixed values. */
class Net : public sensors::Net
{
public:
    /** @brief Fixed network statistics of the interface. */
    Stats stats(const std::string &if_name, std::optional<double> interval) override;
};

sensors::Sensors create();

}  // namespace sensors_stub_static
