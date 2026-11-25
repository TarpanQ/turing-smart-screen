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
 * @file ping.h
 * @brief ICMP echo ("ping"), Uses an unprivileged ICMP socket (allowed by default on Ubuntu, see sysctl net.ipv4.ping_group_range), or a raw socket if the program has the permission. On Windows, uses the ICMP helper API (IcmpSendEcho).
 */

#pragma once

#include <optional>
#include <string>

/** @brief Round-trip delay in milliseconds to the IPv4 host (name or address), or std::nullopt on timeout or error */
std::optional<double> ping(const std::string &dest_addr, double timeout_s = 4.0);
