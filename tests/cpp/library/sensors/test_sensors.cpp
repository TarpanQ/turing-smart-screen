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
 * @file test_sensors.cpp
 * @brief Unit tests of the sensors of src/library/sensors
 */

#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "library/sensors/sensors_custom.h"
#include "library/sensors/sensors_python.h"
#include "library/sensors/sensors_stub_random.h"
#include "library/sensors/sensors_stub_static.h"
#include "library/sensors/sensors_windows.h"

/** @brief Stub sensors with static values return fixed values. */
TEST(SensorsStubStatic, Values)
{
    auto s = sensors_stub_static::create();
    EXPECT_EQ(s.cpu->percentage(std::nullopt), 50.0);
    EXPECT_EQ(s.cpu->frequency(), 2400.0);
    EXPECT_EQ(s.cpu->model(), "Stub CPU model");
    EXPECT_EQ(s.gpu->stats().memory_used_mb, 16384.0);
    EXPECT_EQ(s.gpu->model(), "Stub GPU model");
    EXPECT_EQ(s.memory->virtual_used(), 32000000000);
    EXPECT_EQ(s.memory->model(), "Stub RAM module");
    EXPECT_EQ(s.disk->disk_free(), 500000000000);
    EXPECT_EQ(s.net->stats("eth0", 1).uploaded, 1061000000);
}

/** @brief Stub sensors with random values return values in their range. */
TEST(SensorsStubRandom, Ranges)
{
    auto s = sensors_stub_random::create();
    for (int i = 0; i < 100; i++) {
        const double percentage = s.cpu->percentage(std::nullopt);
        EXPECT_GE(percentage, 0);
        EXPECT_LE(percentage, 100);
        const int fps = s.gpu->fps();
        EXPECT_GE(fps, 20);
        EXPECT_LE(fps, 120);
    }
    EXPECT_FALSE(s.cpu->model().empty());
    EXPECT_FALSE(s.gpu->model().empty());
    EXPECT_FALSE(s.memory->model().empty());
}

/** @brief RAM module model from a SMBIOS Memory Device structure (type 17) and its strings. */
TEST(SensorsPython, MemoryDeviceModel)
{
    // Formatted part of the structure (27 bytes): size at 0x0C, manufacturer string at 0x17, part number at 0x1A
    auto structure = [](uint8_t size_low, uint8_t size_high, uint8_t manufacturer, uint8_t part_number,
                        const std::vector<std::string> &strings) {
        std::vector<uint8_t> raw(27, 0);
        raw[0] = 17;
        raw[1] = 27;
        raw[0x0C] = size_low;
        raw[0x0D] = size_high;
        raw[0x17] = manufacturer;
        raw[0x1A] = part_number;
        for (const std::string &text: strings) {
            raw.insert(raw.end(), text.begin(), text.end());
            raw.push_back(0);
        }
        raw.push_back(0);  // End of the strings
        return raw;
    };

    // Populated module: manufacturer and part number, separated by a space
    EXPECT_EQ(sensors_python::memory_device_model(structure(0x00, 0x20, 1, 2, {"Kingston", "KF3200C16D4/16GX"})),
              "Kingston KF3200C16D4/16GX");
    // Unspecified manufacturer: only the part number
    EXPECT_EQ(sensors_python::memory_device_model(structure(0x00, 0x20, 1, 2, {"Not Specified", "  CT16G4  DFD "})),
              "CT16G4 DFD");
    // Empty slot (size 0), and structure of another type
    EXPECT_FALSE(sensors_python::memory_device_model(structure(0x00, 0x00, 1, 2, {"Kingston", "KF"})).has_value());
    auto other_type = structure(0x00, 0x20, 1, 2, {"Kingston", "KF"});
    other_type[0] = 4;
    EXPECT_FALSE(sensors_python::memory_device_model(other_type).has_value());
    // Truncated structure
    EXPECT_FALSE(sensors_python::memory_device_model({17, 4}).has_value());
}

/** @brief Device name from the pci.ids database. */
TEST(SensorsPython, PciDeviceName)
{
    const std::string pci_ids =
        "# Comment line\n"
        "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n"
        "\t744c  Navi 31 [Radeon RX 7900 XT]\n"
        "\t\t1787 0e6c  Subsystem name\n"
        "10de  NVIDIA Corporation\n"
        "\t2717  GeForce RTX 4080 Laptop GPU\n";
    std::istringstream amd(pci_ids);
    EXPECT_EQ(sensors_python::pci_device_name(amd, 0x1002, 0x744c), "Navi 31 [Radeon RX 7900 XT]");
    std::istringstream nvidia(pci_ids);
    EXPECT_EQ(sensors_python::pci_device_name(nvidia, 0x10de, 0x2717), "GeForce RTX 4080 Laptop GPU");
    // Device of another vendor, and unknown device: empty
    std::istringstream unknown(pci_ids);
    EXPECT_EQ(sensors_python::pci_device_name(unknown, 0x1002, 0x2717), "");
}

#ifndef _WIN32
/** @brief Linux sensors return consistent values. */
TEST(SensorsPython, LinuxValues)
{
    auto s = sensors_python::create();

    s.cpu->percentage(std::nullopt);
    const double percentage = s.cpu->percentage(0.1);
    EXPECT_GE(percentage, 0);
    EXPECT_LE(percentage, 100);
    EXPECT_FALSE(std::isnan(s.cpu->load()[0]));

    EXPECT_GT(s.memory->virtual_used(), 0);
    EXPECT_GT(s.memory->virtual_free(), 0);
    const double memory_percent = s.memory->virtual_percent();
    EXPECT_GT(memory_percent, 0);
    EXPECT_LT(memory_percent, 100);

    EXPECT_GT(s.disk->disk_used(), 0);
    EXPECT_GE(s.disk->disk_usage_percent(), 0);

    // Loopback interface: rates are computed from the second call
    const auto first = s.net->stats("lo", 1.0);
    EXPECT_EQ(first.uploaded, 0);
    const auto second = s.net->stats("lo", 1.0);
    EXPECT_GE(second.uploaded, 0);
    EXPECT_GE(second.upload_rate, 0);
    // Unknown interface: zeros
    EXPECT_EQ(s.net->stats("does-not-exist", 1.0).uploaded, 0);
}
#endif

/** @brief CPU fan detection from the fan label. */
TEST(SensorsPython, CpuFanDetection)
{
    EXPECT_TRUE(sensors_python::is_cpu_fan("CPU Fan"));
    EXPECT_TRUE(sensors_python::is_cpu_fan("processor"));
    EXPECT_FALSE(sensors_python::is_cpu_fan("fan2"));
}

/** @brief Examples of custom data sources. */
TEST(SensorsCustom, Examples)
{
    auto &sources = sensors_custom::custom_data_sources();
    auto &numeric = sources.at("ExampleCustomNumericData");
    EXPECT_EQ(numeric->as_numeric(), 75.845);
    EXPECT_EQ(numeric->as_string(), " 75.8%");
    const auto last_values = numeric->last_values();
    ASSERT_TRUE(last_values.has_value());
    EXPECT_EQ(last_values->size(), 10u);
    EXPECT_EQ(last_values->back(), 75.845);

    auto &text = sources.at("ExampleCustomTextOnlyData");
    EXPECT_FALSE(text->as_numeric().has_value());
    EXPECT_TRUE(text->as_string().has_value());
    EXPECT_FALSE(text->last_values().has_value());
}

/** @brief An SMBIOS table is split into its structures, each one with its strings and its double NUL. */
TEST(SensorsWindows, SmbiosStructures)
{
    const std::vector<uint8_t> bios = {0, 4, 0x00, 0x00, 'A', 'M', 'I', 0, 0};  // Type 0, one string
    const std::vector<uint8_t> memory = {17, 5, 0x11, 0x00, 0x42, 0, 0};       // Type 17, no string
    const std::vector<uint8_t> end = {127, 4, 0x12, 0x00, 0, 0};                // End of table
    std::vector<uint8_t> table;
    for (const auto *part: {&bios, &memory, &end}) {
        table.insert(table.end(), part->begin(), part->end());
    }
    table.insert(table.end(), {1, 2, 3});  // Data after the end of the table: ignored

    const auto structures = sensors_windows::smbios_structures(table);
    ASSERT_EQ(structures.size(), 3u);
    EXPECT_EQ(structures[0], bios);
    EXPECT_EQ(structures[1], memory);
    EXPECT_EQ(structures[2], end);
}

/** @brief A truncated or invalid SMBIOS table gives the complete structures only. */
TEST(SensorsWindows, SmbiosStructuresTruncated)
{
    EXPECT_TRUE(sensors_windows::smbios_structures({}).empty());
    EXPECT_TRUE(sensors_windows::smbios_structures({17, 2, 0, 0, 0, 0}).empty());          // Formatted length < 4
    EXPECT_TRUE(sensors_windows::smbios_structures({17, 30, 0, 0, 0, 0}).empty());         // Longer than the table
    EXPECT_EQ(sensors_windows::smbios_structures({0, 4, 0, 0, 0, 0, 1, 4, 0, 0, 'A'}).size(), 1u);  // No double NUL
}

/** @brief The emulated load averages converge to the processor queue length, the 1 minute one first. */
TEST(SensorsWindows, LoadAverage)
{
    sensors_windows::LoadAverage load;
    EXPECT_EQ(load.values(), (std::array<double, 3>{0, 0, 0}));

    // Same factor as psutil for a 5 s sample: exp(-5/60)
    load.add_sample(4.0, 5.0);
    EXPECT_NEAR(load.values()[0], 4.0 * (1 - std::exp(-5.0 / 60)), 1e-9);
    EXPECT_GT(load.values()[0], load.values()[1]);
    EXPECT_GT(load.values()[1], load.values()[2]);

    // One hour later at the same queue length
    for (int i = 0; i < 720; i++) {
        load.add_sample(4.0, 5.0);
    }
    EXPECT_NEAR(load.values()[0], 4.0, 1e-3);
    EXPECT_NEAR(load.values()[1], 4.0, 1e-3);
    EXPECT_NEAR(load.values()[2], 4.0, 0.1);
}

#ifdef _WIN32
/** @brief Windows sensors return consistent values. */
TEST(SensorsWindows, WindowsValues)
{
    auto s = sensors_windows::create();

    s.cpu->percentage(std::nullopt);
    const double percentage = s.cpu->percentage(0.1);
    EXPECT_GE(percentage, 0);
    EXPECT_LE(percentage, 100);
    EXPECT_GT(s.cpu->frequency(), 0);
    EXPECT_FALSE(s.cpu->model().empty());
    EXPECT_FALSE(std::isnan(s.cpu->load()[0]));

    EXPECT_GT(s.memory->virtual_used(), 0);
    EXPECT_GT(s.memory->virtual_free(), 0);
    const double memory_percent = s.memory->virtual_percent();
    EXPECT_GT(memory_percent, 0);
    EXPECT_LT(memory_percent, 100);
    EXPECT_GE(s.memory->swap_percent(), 0);

    EXPECT_GT(s.disk->disk_used(), 0);
    EXPECT_GT(s.disk->disk_free(), 0);
    EXPECT_GT(s.disk->disk_usage_percent(), 0);

    // Loopback interface: rates are computed from the second call
    const auto first = s.net->stats("Loopback Pseudo-Interface 1", 1.0);
    EXPECT_EQ(first.uploaded, 0);
    const auto second = s.net->stats("Loopback Pseudo-Interface 1", 1.0);
    EXPECT_GE(second.uploaded, 0);
    EXPECT_GE(second.upload_rate, 0);
    // Unknown interface: zeros
    EXPECT_EQ(s.net->stats("does-not-exist", 1.0).uploaded, 0);
}
#endif
