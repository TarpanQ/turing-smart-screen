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
 * @file sensors_lhm.h
 * @brief (HW_SENSORS: LHM, or AUTO as administrator, on Windows) CPU temperature and fan speed read with
 * LibreHardwareMonitor, through the helper program lhm-bridge (src/lhm-bridge/lhm-bridge.cs); the other values come
 * from sensors_windows.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "library/sensors/sensors.h"
#ifdef _WIN32
#include "library/sensors/sensors_windows.h"
#endif

namespace sensors_lhm {

/** @brief A sensor of LibreHardwareMonitor, as written by lhm-bridge. */
struct Sensor
{
    std::string hardware_type;  ///< HardwareType of the hardware (of the parent for a sub-hardware), e.g. "Cpu"
    std::string hardware;       ///< Name of the hardware or sub-hardware, e.g. "Nuvoton NCT6798D"
    std::string type;           ///< SensorType, e.g. "Temperature", "Control"
    std::string name;           ///< Name of the sensor, e.g. "Core Average", "Fan #2"
    double value;               ///< Value, NaN if unknown
};

/**
 * @brief Parse a sensor line written by lhm-bridge: "<hardware type> TAB <hardware> TAB <type> TAB <name> TAB <value>".
 * @return std::nullopt if the line is not a sensor line.
 */
std::optional<Sensor> parse_sensor_line(const std::string &line);

/**
 * @brief CPU temperature, chosen like the Python version: "Core Average", else "Core Max", else "CPU Package", else
 * the first temperature named "Core...", of the CPU. NaN if none.
 */
double cpu_temperature(const std::vector<Sensor> &sensors);

/**
 * @brief CPU fan speed (%): the motherboard fan control named "<hardware>/<sensor>" if fan_name is given, otherwise the
 * fan control "#2" of the motherboard like the Python version. NaN if none.
 */
double cpu_fan_percent(const std::vector<Sensor> &sensors, const std::optional<std::string> &fan_name);

#ifdef _WIN32

/**
 * @brief CPU sensors of sensors_windows, with the temperature and the fan speed read through lhm-bridge.
 * lhm-bridge is started at the first reading; the sensors are read at most once per second.
 */
class Cpu : public sensors_windows::Cpu
{
public:
    /** @brief CPU temperature from LibreHardwareMonitor, NaN if not available. */
    double temperature() override;
    /** @brief CPU fan speed from LibreHardwareMonitor, NaN if not available. */
    double fan_percent(const std::optional<std::string> &fan_name = std::nullopt) override;
};

/** @brief True if lhm-bridge.exe is installed next to this program. */
bool is_available();

/** @brief Create the sensors of sensors_windows, with the CPU temperature and fan speed from LibreHardwareMonitor. */
sensors::Sensors create();

#endif

}  // namespace sensors_lhm
