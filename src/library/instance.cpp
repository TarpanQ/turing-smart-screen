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

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "library/platform.h"

namespace instance {

#ifdef _WIN32

namespace {

// Windows file locks are mandatory: the lock covers a byte far after the PID, so that the PID can still be read
constexpr DWORD LOCK_OFFSET = 1u << 30;

HANDLE open_lock_file(DWORD creation)
{
    return CreateFileW(std::filesystem::path(lock_path()).c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, creation,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool try_lock(HANDLE file)
{
    OVERLAPPED overlapped{};
    overlapped.Offset = LOCK_OFFSET;
    return LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped);
}

void unlock(HANDLE file)
{
    OVERLAPPED overlapped{};
    overlapped.Offset = LOCK_OFFSET;
    UnlockFileEx(file, 0, 1, 0, &overlapped);
}

}  // namespace

std::string lock_path()
{
    return (std::filesystem::temp_directory_path() / "turing-system-monitor.lock").string();
}

int acquire_lock()
{
    HANDLE file = open_lock_file(OPEN_ALWAYS);
    if (file == INVALID_HANDLE_VALUE) {
        return -2;
    }
    if (!try_lock(file)) {
        CloseHandle(file);
        return -1;
    }
    // The PID is written so that the configuration program can stop this instance
    if (SetFilePointer(file, 0, nullptr, FILE_BEGIN) == 0 && SetEndOfFile(file)) {
        const std::string pid = std::to_string(platform::process_id()) + "\n";
        DWORD written = 0;
        WriteFile(file, pid.data(), static_cast<DWORD>(pid.size()), &written, nullptr);
    }
    // The handle is kept open (and locked) until the process exits; Windows handles fit in an int
    return static_cast<int>(reinterpret_cast<intptr_t>(file));
}

namespace {

// True if an instance holds the lock. The lock is only tested here: it is released at once if nobody holds it.
bool is_running()
{
    HANDLE file = open_lock_file(OPEN_EXISTING);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool running = !try_lock(file);
    if (!running) {
        unlock(file);
    }
    CloseHandle(file);
    return running;
}

}  // namespace

#else  // Linux

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

#endif

bool stop_running(std::chrono::seconds timeout)
{
    if (!is_running()) {
        return true;
    }

    std::ifstream file(lock_path());
    int pid = 0;
    file >> pid;
    if (pid > 0) {
        platform::send_stop_request(pid);
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
