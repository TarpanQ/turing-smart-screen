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
 * @file test_instance.cpp
 * @brief Unit tests of the single instance lock and of the stop of the running instance (src/library/instance).
 */

#include <gtest/gtest.h>

// These tests use fork(): Linux only. On Windows, the stop requests are tested in test_platform.cpp
#ifndef _WIN32

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "library/instance.h"

namespace {

// Runtime directory used by the tests, so that the real lock of the system monitor is not touched
void use_temporary_runtime_dir()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "test_instance_runtime";
    std::filesystem::create_directories(dir);
    setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
}

}  // namespace

/** @brief A second instance cannot take the lock, and stopping the running instance frees it. */
TEST(Instance, StopRunningInstance)
{
    use_temporary_runtime_dir();

    // Nothing running: nothing to stop
    EXPECT_TRUE(instance::stop_running(std::chrono::seconds(1)));

    // A child process runs as an instance: it takes the lock and waits
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        if (instance::acquire_lock() < 0) {
            _exit(2);
        }
        pause();
        _exit(0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(instance::acquire_lock(), -1);
    EXPECT_TRUE(instance::stop_running(std::chrono::seconds(5)));

    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);

    // The lock is free again
    const int fd = instance::acquire_lock();
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
        close(fd);
    }
}

/**
 * @brief The "elevated" flag that Windows writes after the PID is read back; without it, the instance is not elevated.
 */
TEST(Instance, RunningElevated)
{
    use_temporary_runtime_dir();

    // Nothing running
    EXPECT_FALSE(instance::running_elevated());

    // This process holds the lock: the flag is read from the lock file
    const int fd = instance::acquire_lock();
    ASSERT_GE(fd, 0);
    EXPECT_FALSE(instance::running_elevated());  // Linux never writes the flag
    std::ofstream(instance::lock_path()) << ::getpid() << " elevated\n";
    EXPECT_TRUE(instance::running_elevated());
    std::ofstream(instance::lock_path()) << ::getpid() << "\n";
    EXPECT_FALSE(instance::running_elevated());
    close(fd);
}

#endif
