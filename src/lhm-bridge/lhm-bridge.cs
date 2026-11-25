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

// lhm-bridge.cs: helper program of the system monitor on Windows (HW_SENSORS: LHM, or AUTO as administrator).
// It reads the hardware sensors with LibreHardwareMonitorLib.dll (.NET Framework 4.7.2), like the Python version, and
// sends them to the system monitor through its standard input and output:
//   - started with the directory of LibreHardwareMonitorLib.dll and its dependencies as only argument
//   - writes "ready" once the sensors are opened, or "error<TAB>message" and exits
//   - for each "update" line read: updates all the hardware, writes one line per sensor
//     "<hardware type><TAB><hardware name><TAB><sensor type><TAB><sensor name><TAB><value>" (value empty if unknown,
//     sub-hardware with the type of its parent hardware), then "end"
//   - exits on "quit" or at the end of its input (the system monitor stopped)
// Built with the C# compiler of the .NET Framework (C# 5): see the Makefile.

using System;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using LibreHardwareMonitor.Hardware;

namespace TuringSmartScreen
{
    /// <summary>
    /// Bridge between LibreHardwareMonitorLib and the system monitor, through standard input and output.
    /// </summary>
    public static class LhmBridge
    {
        private static string lhmDirectory;

        /// <summary>Entry point: args[0] is the directory of LibreHardwareMonitorLib.dll.</summary>
        public static int Main(string[] args)
        {
            TextWriter output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false));
            output.NewLine = "\n";
            if (args.Length != 1)
            {
                Console.Error.WriteLine("usage: lhm-bridge <directory of LibreHardwareMonitorLib.dll>");
                return 2;
            }
            lhmDirectory = Path.GetFullPath(args[0]);
            // The library and its dependencies are loaded from their directory, whatever versions they reference
            AppDomain.CurrentDomain.AssemblyResolve += ResolveFromLhmDirectory;
            try
            {
                return Run(new StreamReader(Console.OpenStandardInput(), Encoding.UTF8), output);
            }
            catch (Exception e)
            {
                output.WriteLine("error\t" + Clean(e.Message));
                output.Flush();
                return 1;
            }
        }

        private static Assembly ResolveFromLhmDirectory(object sender, ResolveEventArgs e)
        {
            string file = Path.Combine(lhmDirectory, new AssemblyName(e.Name).Name + ".dll");
            return File.Exists(file) ? Assembly.LoadFrom(file) : null;
        }

        // Not inlined: the LibreHardwareMonitor types are loaded when this method is compiled, after AssemblyResolve
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static int Run(TextReader input, TextWriter output)
        {
            Computer computer = new Computer();
            computer.IsCpuEnabled = true;
            computer.IsGpuEnabled = true;
            computer.IsMemoryEnabled = true;
            computer.IsMotherboardEnabled = true;  // For the CPU fan speed
            computer.IsControllerEnabled = true;   // For the CPU fan speed
            computer.Open();
            output.WriteLine("ready");
            output.Flush();

            string line;
            while ((line = input.ReadLine()) != null && line != "quit")
            {
                if (line != "update")
                {
                    continue;
                }
                foreach (IHardware hardware in computer.Hardware)
                {
                    hardware.Update();
                    WriteSensors(output, hardware.HardwareType, hardware);
                    foreach (IHardware subHardware in hardware.SubHardware)
                    {
                        subHardware.Update();
                        WriteSensors(output, hardware.HardwareType, subHardware);
                    }
                }
                output.WriteLine("end");
                output.Flush();
            }
            computer.Close();
            return 0;
        }

        private static void WriteSensors(TextWriter output, HardwareType type, IHardware hardware)
        {
            foreach (ISensor sensor in hardware.Sensors)
            {
                string value = sensor.Value.HasValue
                                   ? sensor.Value.Value.ToString("R", CultureInfo.InvariantCulture)
                                   : "";
                output.WriteLine(type + "\t" + Clean(hardware.Name) + "\t" + sensor.SensorType + "\t" +
                                 Clean(sensor.Name) + "\t" + value);
            }
        }

        // The fields are separated by tabs and the sensors by new lines: they are replaced in the names
        private static string Clean(string text)
        {
            return text == null ? "" : text.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ');
        }
    }
}
