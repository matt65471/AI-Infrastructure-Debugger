#include "pressure_collector.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << content;
}

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool near(double actual, double expected) {
    return std::abs(actual - expected) < 0.0001;
}

}  // namespace

int main() {
    const auto unique_suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path proc_root =
        std::filesystem::temp_directory_path() /
        ("pressure_collector_test_" + std::to_string(unique_suffix));

    write_file(proc_root / "pressure/cpu",
               "some avg10=1.25 avg60=0.75 avg300=0.20 total=12345\n");
    write_file(proc_root / "pressure/memory",
               "some avg10=2.50 avg60=1.50 avg300=0.50 total=23456\n"
               "full avg10=0.25 avg60=0.15 avg300=0.05 total=3456\n");
    write_file(proc_root / "pressure/io",
               "some avg10=3.50 avg60=2.50 avg300=1.50 total=34567\n"
               "full avg10=1.00 avg60=0.50 avg300=0.25 total=4567\n");
    write_file(proc_root / "loadavg", "0.10 0.20 0.30 3/120 999\n");

    const PressureSample sample = PressureCollector(proc_root).read_sample();

    bool passed = true;
    passed &= expect(sample.cpu.available && !sample.cpu.full_available,
                     "parses CPU some pressure without inventing full pressure");
    passed &= expect(near(sample.cpu.some.avg10, 1.25) &&
                         sample.cpu.some.total_usec == 12345,
                     "parses CPU pressure values");
    passed &= expect(sample.memory.available && sample.memory.full_available &&
                         near(sample.memory.full.avg10, 0.25),
                     "parses memory some and full pressure");
    passed &= expect(sample.io.available && sample.io.full_available &&
                         sample.io.full.total_usec == 4567,
                     "parses IO some and full pressure");
    passed &= expect(sample.load_average_available &&
                         near(sample.load_average_1m, 0.10) &&
                         near(sample.load_average_15m, 0.30),
                     "parses load averages");
    passed &= expect(sample.runnable_processes == 3 &&
                         sample.total_processes == 120,
                     "parses runnable and total process counts");

    std::filesystem::remove_all(proc_root);
    return passed ? 0 : 1;
}
