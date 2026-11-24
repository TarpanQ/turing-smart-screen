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
 * @file sensors_stub_random.h
 * @brief This file will use randomly generated data instead of real hardware sensors
 */

#pragma once

#include "library/sensors/sensors.h"

namespace sensors_stub_random {

/** @brief CPU sensors returning random values. */
class Cpu : public sensors::Cpu
{
public:
    /** @brief Random CPU usage, in percent. */
    double percentage(std::optional<double> interval) override;
    /** @brief Random CPU frequency, in MHz. */
    double frequency() override;
    /** @brief Random system load averages. */
    std::array<double, 3> load() override;
    /** @brief Random CPU temperature, in degrees Celsius. */
    double temperature() override;
    /** @brief Random CPU fan speed, in percent. */
    double fan_percent(const std::optional<std::string> &fan_name = std::nullopt) override;
};

/** @brief GPU sensors returning random values. */
class Gpu : public sensors::Gpu
{
public:
    /** @brief Random GPU statistics. */
    Stats stats() override;
    /** @brief Random frames per second. */
    int fps() override;
    /** @brief Random GPU fan speed, in percent. */
    double fan_percent() override;
    /** @brief Random GPU frequency, in MHz. */
    double frequency() override;
    /** @brief The random GPU is always available. */
    bool is_available() override;
};

/** @brief Memory sensors returning random values. */
class Memory : public sensors::Memory
{
public:
    /** @brief Random swap usage, in percent. */
    double swap_percent() override;
    /** @brief Random RAM usage, in percent. */
    double virtual_percent() override;
    /** @brief Random RAM used, in bytes. */
    int64_t virtual_used() override;
    /** @brief Random RAM free, in bytes. */
    int64_t virtual_free() override;
};

/** @brief Disk sensors returning random values. */
class Disk : public sensors::Disk
{
public:
    /** @brief Random disk usage, in percent. */
    double disk_usage_percent() override;
    /** @brief Random disk used, in bytes. */
    int64_t disk_used() override;
    /** @brief Random disk free, in bytes. */
    int64_t disk_free() override;
};

/** @brief Network sensors returning random values. */
class Net : public sensors::Net
{
public:
    /** @brief Random network statistics of the interface. */
    Stats stats(const std::string &if_name, std::optional<double> interval) override;
};

sensors::Sensors create();

}  // namespace sensors_stub_random
