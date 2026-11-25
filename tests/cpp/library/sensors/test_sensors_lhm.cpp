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
 * @file test_sensors_lhm.cpp
 * @brief Unit tests of the LibreHardwareMonitor sensors (src/library/sensors/sensors_lhm): parsing of the lhm-bridge
 * output and choice of the sensors, on all platforms.
 */

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "library/sensors/sensors_lhm.h"

namespace {

std::vector<sensors_lhm::Sensor> parse(const std::vector<std::string> &lines)
{
    std::vector<sensors_lhm::Sensor> sensors;
    for (const std::string &line: lines) {
        if (const auto sensor = sensors_lhm::parse_sensor_line(line)) {
            sensors.push_back(*sensor);
        }
    }
    return sensors;
}

}  // namespace

/** @brief A sensor line has 5 fields; an empty value is NaN; other lines are ignored. */
TEST(SensorsLhm, ParseSensorLine)
{
    const auto sensor = sensors_lhm::parse_sensor_line("Cpu\tIntel Core i7-9700K\tTemperature\tCore Average\t45.25");
    ASSERT_TRUE(sensor.has_value());
    EXPECT_EQ(sensor->hardware_type, "Cpu");
    EXPECT_EQ(sensor->hardware, "Intel Core i7-9700K");
    EXPECT_EQ(sensor->type, "Temperature");
    EXPECT_EQ(sensor->name, "Core Average");
    EXPECT_DOUBLE_EQ(sensor->value, 45.25);

    EXPECT_DOUBLE_EQ(sensors_lhm::parse_sensor_line("Cpu\tCPU\tLoad\tCPU Total\t1.5E-05")->value, 1.5e-05);
    EXPECT_TRUE(std::isnan(sensors_lhm::parse_sensor_line("Cpu\tCPU\tTemperature\tCore #1\t")->value));
    EXPECT_TRUE(std::isnan(sensors_lhm::parse_sensor_line("Cpu\tCPU\tTemperature\tCore #1\tNaN")->value));

    for (const char *line: {"ready", "end", "error\tsomething failed", "Cpu\tCPU\tLoad\t1", "\tCPU\tLoad\tx\t1"}) {
        EXPECT_FALSE(sensors_lhm::parse_sensor_line(line).has_value()) << line;
    }
}

/** @brief The CPU temperature is chosen like the Python version, on the first CPU only. */
TEST(SensorsLhm, CpuTemperature)
{
    std::vector<std::string> lines = {
        "Cpu\tCPU A\tTemperature\tCore #1\t50",
        "Cpu\tCPU A\tTemperature\tCPU Package\t58",
        "Cpu\tCPU A\tTemperature\tCore Max\t55",
        "Cpu\tCPU A\tTemperature\tCore Average\t47",
        "Cpu\tCPU B\tTemperature\tCore Average\t90",
        "GpuNvidia\tGeForce\tTemperature\tGPU Core\t70",
    };
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_temperature(parse(lines)), 47);
    lines.erase(lines.begin() + 3);  // No Core Average: the maximum
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_temperature(parse(lines)), 55);
    lines.erase(lines.begin() + 2);  // No Core Max: the package
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_temperature(parse(lines)), 58);
    lines.erase(lines.begin() + 1);  // No package: any core
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_temperature(parse(lines)), 50);

    // Unknown values are skipped; no CPU temperature: NaN
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_temperature(parse({"Cpu\tCPU\tTemperature\tCore Average\t",
                                                         "Cpu\tCPU\tTemperature\tCore Max\t61"})),
                     61);
    EXPECT_TRUE(std::isnan(sensors_lhm::cpu_temperature(parse({"GpuNvidia\tGeForce\tTemperature\tGPU Core\t70"}))));
    EXPECT_TRUE(std::isnan(sensors_lhm::cpu_temperature({})));
}

/** @brief The CPU fan is the fan control #2 of the motherboard, or the fan selected by its name. */
TEST(SensorsLhm, CpuFanPercent)
{
    const auto sensors = parse({
        "Motherboard\tNuvoton NCT6798D\tFan\tFan #2\t1200",
        "Motherboard\tNuvoton NCT6798D\tControl\tFan #1\t30",
        "Motherboard\tNuvoton NCT6798D\tControl\tFan #2\t45",
        "GpuNvidia\tGeForce\tControl\tGPU Fan #2\t60",
    });
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_fan_percent(sensors, std::nullopt), 45);
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_fan_percent(sensors, "Nuvoton NCT6798D/Fan #1"), 30);
    // Unknown fan name: automatic choice
    EXPECT_DOUBLE_EQ(sensors_lhm::cpu_fan_percent(sensors, "Nuvoton NCT6798D/Fan #7"), 45);
    EXPECT_TRUE(std::isnan(sensors_lhm::cpu_fan_percent(parse({"GpuNvidia\tGeForce\tControl\tGPU Fan #2\t60"}),
                                                        std::nullopt)));
}
