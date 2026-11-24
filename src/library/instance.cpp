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
 * @file instance.cpp
 */

#include "library/instance.h"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <thread>
#include <unistd.h>

namespace instance {

std::string lock_path()
{
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    return std::string(runtime_dir != nullptr ? runtime_dir : "/tmp") + "/turing-system-monitor.lock";
}

int acquire_lock()
{
    const int fd = open(lock_path().c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) {
        return -2;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }
    // The PID is written so that the configuration program can stop this instance
    if (ftruncate(fd, 0) == 0) {
        const std::string pid = std::to_string(getpid()) + "\n";
        [[maybe_unused]] const ssize_t written = write(fd, pid.data(), pid.size());
    }
    return fd;
}

namespace {

// True if an instance holds the lock. The lock is only tested here: it is released at once if nobody holds it.
bool is_running()
{
    const int fd = open(lock_path().c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool running = flock(fd, LOCK_EX | LOCK_NB) != 0;
    if (!running) {
        flock(fd, LOCK_UN);
    }
    close(fd);
    return running;
}

}  // namespace

bool stop_running(std::chrono::seconds timeout)
{
    if (!is_running()) {
        return true;
    }

    std::ifstream file(lock_path());
    pid_t pid = 0;
    file >> pid;
    if (pid > 0) {
        kill(pid, SIGTERM);
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (is_running()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}

}  // namespace instance
