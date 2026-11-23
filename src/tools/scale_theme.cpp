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

// C++ port of res/themes/scale_theme.py: Rescale a turing-smart-screen theme YAML to a new resolution.
//
// Usage:
//     scale_theme theme.yaml output.yaml --from 480x800 --to 800x1280
//     scale_theme theme.yaml output.yaml --from 800x480 --to 1280x800

#include <cfenv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::set<std::string> X_KEYS = {"X", "RADIUS"};  // scaled by x_factor
const std::set<std::string> Y_KEYS = {"Y"};  // scaled by y_factor
const std::set<std::string> W_KEYS = {"WIDTH"};  // scaled by x_factor
const std::set<std::string> H_KEYS = {"HEIGHT"};  // scaled by y_factor
const std::set<std::string> FS_KEYS = {"FONT_SIZE", "AXIS_FONT_SIZE"};  // scaled by avg factor

// Parse 'WxH' string into (width, height)
std::pair<int, int> parse_resolution(const std::string &res)
{
    static const std::regex pattern(R"(\s*(\d+)(?:x|X|×)(\d+)\s*)");
    std::smatch match;
    if (!std::regex_match(res, match, pattern)) {
        throw std::invalid_argument("Invalid resolution format '" + res + "'. Expected WxH (e.g. 800x480).");
    }
    return {std::stoi(match[1]), std::stoi(match[2])};
}

// Apply factor and round to nearest integer (ties to even, like Python round())
long scale_value(double value, double factor)
{
    std::fesetround(FE_TONEAREST);
    return std::lrint(value * factor);
}

// Walk through YAML lines and rescale numeric values for known keys.
// Preserves comments, indentation, and all other content exactly.
std::vector<std::string> process_lines(const std::vector<std::string> &lines, double fx, double fy)
{
    // [indent]KEY: number[optional comment]
    static const std::regex pattern(R"(^(\s*)(\w+)(\s*:\s*)(-?\d+(?:\.\d+)?)(.*))");
    const double favg = (fx + fy) / 2;
    std::vector<std::string> result;
    for (const auto &line: lines) {
        // The line ending is not part of the matched text
        const size_t eol = line.find('\n');
        const std::string content = line.substr(0, eol);
        std::smatch m;
        if (!std::regex_match(content, m, pattern)) {
            result.push_back(line);
            continue;
        }
        const std::string indent = m[1], key = m[2], sep = m[3], raw_val = m[4], rest = m[5];
        const double val = std::stod(raw_val);
        long new_val;
        if (X_KEYS.contains(key) || W_KEYS.contains(key)) {
            new_val = scale_value(val, fx);
        } else if (Y_KEYS.contains(key) || H_KEYS.contains(key)) {
            new_val = scale_value(val, fy);
        } else if (FS_KEYS.contains(key)) {
            new_val = scale_value(val, favg);
        } else {
            result.push_back(line);
            continue;
        }
        // Preserve original int/float representation
        if (raw_val.find('.') != std::string::npos) {
            result.push_back(std::format("{}{}{}{}.0{}\n", indent, key, sep, new_val, rest));
        } else {
            result.push_back(std::format("{}{}{}{}{}\n", indent, key, sep, new_val, rest));
        }
    }
    return result;
}

void scale_theme(const fs::path &src, const fs::path &dst, const std::string &src_res, const std::string &dst_res)
{
    const auto [sw, sh] = parse_resolution(src_res);
    const auto [dw, dh] = parse_resolution(dst_res);
    const double fx = static_cast<double>(dw) / sw;
    const double fy = static_cast<double>(dh) / sh;

    std::cout << "Source      : " << src.string() << "\n";
    std::cout << "Destination : " << dst.string() << "\n";
    std::cout << std::format("From        : {}×{}\n", sw, sh);
    std::cout << std::format("To          : {}×{}\n", dw, dh);
    std::cout << std::format("X factor    : {:.6f}   ({} → {})\n", fx, sw, dw);
    std::cout << std::format("Y factor    : {:.6f}   ({} → {})\n", fy, sh, dh);
    std::cout << std::format("Font factor : {:.6f}  (average)\n", (fx + fy) / 2);

    std::ifstream in(src, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> lines;
    size_t start = 0, end;
    while ((end = text.find('\n', start)) != std::string::npos) {
        lines.push_back(text.substr(start, end - start + 1));
        start = end + 1;
    }
    if (start < text.size()) {
        lines.push_back(text.substr(start));
    }

    std::ofstream out(dst, std::ios::binary);
    for (const auto &line: process_lines(lines, fx, fy)) {
        out << line;
    }
    std::cout << "\nDone — written to " << dst.string() << "\n";
}

void usage(const char *program)
{
    std::cerr << "usage: " << program << " [-h] --from WxH --to WxH input output\n";
}

}  // namespace

int main(int argc, char *argv[])
{
    std::vector<std::string> positional;
    std::string src_res, dst_res;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            std::cout << "\nRescale a turing-smart-screen theme YAML to a new resolution.\n\n"
                         "positional arguments:\n"
                         "  input       Source theme.yaml\n"
                         "  output      Output theme.yaml\n\n"
                         "options:\n"
                         "  --from WxH  Original resolution, e.g. 480x800\n"
                         "  --to WxH    Target resolution,   e.g. 800x1280\n";
            return 0;
        } else if ((arg == "--from" || arg == "--to") && i + 1 < argc) {
            (arg == "--from" ? src_res : dst_res) = argv[++i];
        } else if (arg.starts_with("--from=")) {
            src_res = arg.substr(7);
        } else if (arg.starts_with("--to=")) {
            dst_res = arg.substr(5);
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2 || src_res.empty() || dst_res.empty()) {
        usage(argv[0]);
        std::cerr << argv[0] << ": error: the arguments input, output, --from and --to are required\n";
        return 2;
    }

    const fs::path input = positional[0], output = positional[1];
    if (!fs::exists(input)) {
        std::cerr << "Error: file not found: " << input.string() << "\n";
        return 1;
    }

    try {
        scale_theme(input, output, src_res, dst_res);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
