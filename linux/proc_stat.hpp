
#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct ProcEvent {
  void start_counters() { start_values = read_cpu_stats(); }

  void stop_counters() {
    end_values = read_cpu_stats();
    valid = !start_values.empty();
  }
  std::string to_string() {
    std::stringstream ss;
    unsigned int num_vcpus = std::thread::hardware_concurrency();
    ss << "vcpus,\tuser,\tnice,\tsystem,\tidle,\tiowait,\tirq,\tsoftirq,\tsteal,\tguest"
       << std::endl;
    std::vector<long> diffs;
    uint64_t total = 0;
    for (size_t pos = 0; pos < start_values.size(); ++pos) {
      diffs.push_back(end_values[pos] - start_values[pos]);
      total += diffs.back();
    }
    ss << num_vcpus << ",\t";
    for (size_t pos = 0; pos < diffs.size(); ++pos) {
      ss << (diffs[pos] * 1.0 / total) * num_vcpus;
      if (pos != diffs.size() - 1) {
        ss << ",\t";
      }
    }
    ss << std::endl;
    return ss.str();
  }

 private:
  std::vector<long> read_cpu_stats() {
    std::ifstream file("/proc/stat");
    std::string line;
    getline(file, line);
    file.close();
    std::vector<long> times;
    size_t start = line.find_first_of("0123456789");
    while (start != std::string::npos) {
      size_t end = line.find_first_not_of("0123456789", start);
      times.push_back(stol(line.substr(start, end - start)));
      start = line.find_first_of("0123456789", end);
    }
    return times;
  }
  bool valid = false;
  std::vector<long> start_values;
  std::vector<long> end_values;
};
