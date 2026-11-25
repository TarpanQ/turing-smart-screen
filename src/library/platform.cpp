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
 * @file platform.cpp
 * @brief Operating system services that differ between Linux and Windows, behind a common API.
 */

#include "library/platform.h"

#include <algorithm>
#include <fstream>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>

#include <atomic>
#include <mutex>
#else
#include <csignal>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <pthread.h>
#include <spawn.h>
#include <unistd.h>

/** @brief Environment variables of the process. */
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace platform {

fs::path program_path(const std::string &name)
{
    const fs::path exe = executable_path();
    const fs::path directory = exe.empty() ? fs::current_path() : exe.parent_path();
    return directory / (name + EXECUTABLE_SUFFIX);
}

#ifdef _WIN32

std::string to_utf8(const wchar_t *text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

namespace {

/** @brief Value of stop_reason when the stop was requested by request_stop(). */
constexpr int STOP_FROM_THIS_PROGRAM = -1;
/** @brief Value of stop_reason when the stop was requested by send_stop_request() from another program. */
constexpr int STOP_FROM_OTHER_PROGRAM = -2;

/** @brief Event set to stop this program (manual reset), created by prepare_stop_requests(). */
HANDLE stop_event = nullptr;
/** @brief Console control event (CTRL_C_EVENT...), or STOP_FROM_THIS_PROGRAM / STOP_FROM_OTHER_PROGRAM. */
std::atomic<int> stop_reason = STOP_FROM_OTHER_PROGRAM;

/** @brief Name of the stop event of a process: other programs of the session open it to stop the process. */
std::wstring stop_event_name(int pid)
{
    return L"Local\\turing-smart-screen-stop-" + std::to_wstring(pid);
}

BOOL WINAPI console_handler(DWORD type)
{
    stop_reason = static_cast<int>(type);
    SetEvent(stop_event);
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        // Windows ends the process when this function returns: the main thread stops the program and exits first
        Sleep(INFINITE);
    }
    return TRUE;
}

}  // namespace

fs::path executable_path()
{
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0) {
            return {};
        }
        if (size < buffer.size()) {
            buffer.resize(size);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);  // Path truncated: try again with a larger buffer
    }
}

int process_id()
{
    return static_cast<int>(GetCurrentProcessId());
}

double uptime_seconds()
{
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

std::tm local_time(std::time_t t)
{
    std::tm tm{};
    localtime_s(&tm, &t);
    return tm;
}

std::vector<std::string> network_interface_names()
{
    std::vector<std::string> names;
    // The buffer size is unknown: the call is repeated with the size returned
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer;
    ULONG ret = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && ret == ERROR_BUFFER_OVERFLOW; attempt++) {
        buffer.resize(size);
        constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()),
                                   &size);
    }
    if (ret != NO_ERROR) {
        return names;
    }
    // Same names as psutil on Windows: the "friendly name" of the adapters, for example "Ethernet" or "Wi-Fi"
    for (auto *a = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()); a; a = a->Next) {
        const std::string name = to_utf8(a->FriendlyName);
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    return names;
}

void init_sockets()
{
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

void *load_library(const char *name)
{
    return reinterpret_cast<void *>(LoadLibraryA(name));
}

void *find_symbol(void *library, const char *name)
{
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(library), name));
}

bool start_program(const fs::path &program)
{
    // Command line: the program path, quoted (it may contain spaces)
    std::wstring command_line = L"\"" + program.wstring() + L"\"";
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup_info, &process_info)) {
        return false;
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
}

void prepare_stop_requests()
{
    stop_event = CreateEventW(nullptr, TRUE, FALSE, stop_event_name(process_id()).c_str());
    SetConsoleCtrlHandler(console_handler, TRUE);
}

std::string wait_for_stop_request()
{
    WaitForSingleObject(stop_event, INFINITE);
    const int reason = stop_reason;
    if (reason == STOP_FROM_THIS_PROGRAM) {
        return {};
    }
    if (reason == STOP_FROM_OTHER_PROGRAM) {
        return "Stop requested by another program";
    }
    return "Caught console event " + std::to_string(reason);
}

void request_stop()
{
    stop_reason = STOP_FROM_THIS_PROGRAM;
    SetEvent(stop_event);
}

bool send_stop_request(int pid)
{
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, stop_event_name(pid).c_str());
    if (!event) {
        return false;
    }
    const bool result = SetEvent(event);
    CloseHandle(event);
    return result;
}

#else  // Linux

namespace {

/** @brief Signal sent to this process by request_stop() (handled like the other stopping signals). */
constexpr int STOP_FROM_THIS_PROGRAM_SIGNAL = SIGUSR1;

/** @brief Signals stopping the program, handled by wait_for_stop_request(). */
sigset_t stop_signals()
{
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGQUIT);
    sigaddset(&signals, STOP_FROM_THIS_PROGRAM_SIGNAL);
    return signals;
}

}  // namespace

fs::path executable_path()
{
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : exe;
}

int process_id()
{
    return static_cast<int>(::getpid());
}

double uptime_seconds()
{
    std::ifstream file("/proc/uptime");
    double seconds = 0;
    file >> seconds;
    return seconds;
}

std::tm local_time(std::time_t t)
{
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

std::vector<std::string> network_interface_names()
{
    std::vector<std::string> names;
    ifaddrs *addresses = nullptr;
    if (getifaddrs(&addresses) == 0) {
        for (ifaddrs *a = addresses; a; a = a->ifa_next) {
            if (a->ifa_name && std::find(names.begin(), names.end(), a->ifa_name) == names.end()) {
                names.emplace_back(a->ifa_name);
            }
        }
        freeifaddrs(addresses);
    }
    return names;
}

void init_sockets()
{
}

void *load_library(const char *name)
{
    return dlopen(name, RTLD_NOW);
}

void *find_symbol(void *library, const char *name)
{
    return dlsym(library, name);
}

bool start_program(const fs::path &program)
{
    // The stopping signals may be blocked in this program (prepare_stop_requests): they must not be blocked in the
    // new process
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t no_signals;
    sigemptyset(&no_signals);
    posix_spawnattr_setsigmask(&attr, &no_signals);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
    const std::string file = program.string();
    char *const spawn_argv[] = {const_cast<char *>(file.c_str()), nullptr};
    pid_t pid;
    const int ret = posix_spawn(&pid, file.c_str(), nullptr, &attr, spawn_argv, environ);
    posix_spawnattr_destroy(&attr);
    return ret == 0;
}

void prepare_stop_requests()
{
    // The stopping signals are handled by the main thread (wait_for_stop_request): they are blocked in all threads,
    // including the threads created later that inherit this mask
    const sigset_t signals = stop_signals();
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
}

std::string wait_for_stop_request()
{
    const sigset_t signals = stop_signals();
    int signum = 0;
    sigwait(&signals, &signum);
    if (signum == STOP_FROM_THIS_PROGRAM_SIGNAL) {
        return {};
    }
    return "Caught signal " + std::to_string(signum);
}

void request_stop()
{
    kill(getpid(), STOP_FROM_THIS_PROGRAM_SIGNAL);
}

bool send_stop_request(int pid)
{
    return pid > 0 && kill(pid, SIGTERM) == 0;
}

#endif

}  // namespace platform
