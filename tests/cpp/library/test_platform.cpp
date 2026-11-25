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
 * @file test_platform.cpp
 * @brief Unit tests of the operating system services (src/library/platform).
 */

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <set>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "library/platform.h"

/** @brief The path of the running program is found: the unit tests program. */
TEST(Platform, ExecutablePath)
{
    const std::filesystem::path exe = platform::executable_path();
    ASSERT_FALSE(exe.empty());
    EXPECT_TRUE(exe.is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(exe));
    EXPECT_EQ(exe.filename().string(), std::string("unit-tests") + platform::EXECUTABLE_SUFFIX);
}

/** @brief The other programs are in the directory of the running program, with the executable suffix. */
TEST(Platform, ProgramPath)
{
    const std::filesystem::path configure = platform::program_path("configure");
    EXPECT_EQ(configure.parent_path(), platform::executable_path().parent_path());
    EXPECT_EQ(configure.filename().string(), std::string("configure") + platform::EXECUTABLE_SUFFIX);
}

/** @brief Process identifier and uptime of the system are available. */
TEST(Platform, ProcessAndUptime)
{
    EXPECT_GT(platform::process_id(), 0);
    EXPECT_GT(platform::uptime_seconds(), 0);
}

#ifndef _WIN32
/** @brief Administrator rights: root on Linux. */
TEST(Platform, IsElevated)
{
    EXPECT_EQ(platform::is_elevated(), ::geteuid() == 0);
}
#endif

/** @brief The local time is the inverse of mktime. */
TEST(Platform, LocalTime)
{
    const std::time_t now = std::time(nullptr);
    std::tm tm = platform::local_time(now);
    EXPECT_GE(tm.tm_year, 125);
    EXPECT_EQ(std::mktime(&tm), now);
}

/** @brief Network interfaces are listed once each. */
TEST(Platform, NetworkInterfaces)
{
    const auto names = platform::network_interface_names();
    EXPECT_FALSE(names.empty());
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()).size(), names.size());
    EXPECT_TRUE(std::none_of(names.begin(), names.end(), [](const std::string &name) { return name.empty(); }));
}

/** @brief Shared libraries are loaded and their functions are found; a missing library is reported. */
TEST(Platform, SharedLibrary)
{
    EXPECT_EQ(platform::load_library("does-not-exist-library"), nullptr);

#ifdef _WIN32
    void *library = platform::load_library("kernel32.dll");
    const char *function = "GetTickCount64";
#else
    void *library = platform::load_library("libc.so.6");
    const char *function = "getpid";
#endif
    ASSERT_NE(library, nullptr);
    EXPECT_NE(platform::find_symbol(library, function), nullptr);
    EXPECT_EQ(platform::find_symbol(library, "does_not_exist_function"), nullptr);
}

#ifdef _WIN32
/**
 * @brief A stop request sent to this process ends the wait. On Linux, the stop requests are signals: they are tested
 * with another process in test_instance.cpp.
 */
TEST(Platform, StopRequest)
{
    platform::prepare_stop_requests();
    EXPECT_TRUE(platform::send_stop_request(platform::process_id()));
    EXPECT_EQ(platform::wait_for_stop_request(), "Stop requested by another program");
    platform::request_stop();
    EXPECT_EQ(platform::wait_for_stop_request(), "");

    // No process with this identifier: the request cannot be sent
    EXPECT_FALSE(platform::send_stop_request(0));
}
#endif
