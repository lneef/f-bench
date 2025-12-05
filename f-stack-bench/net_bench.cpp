#include <fmt/format.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <vector>

#include "fstack.hpp"
#include "proc_stat.hpp"
#include "settings.hpp"
#include "utils.hpp"

#include "ff_api.h"

constexpr uint64_t PORT = 8000;

void benchmarker_mt(std::vector<int>& fds, Direction dir, uint64_t seconds) {
  std::vector<uint64_t> bytes(fds.size(), 0);
  std::atomic<int> start(0);
  std::atomic<int> running(1);
  std::atomic<int> done(0);
  std::vector<std::thread> thread_pool;
  std::vector<char*> data1(fds.size(), nullptr);
  for (int fd_id = 0; fd_id < static_cast<int>(fds.size()); ++fd_id) {
    data1[fd_id] = reinterpret_cast<char*>(malloc(DATA_LENGTH));
  }
  auto do_work = [&](uint64_t id, bool rcv) {
    int fd = fds[id];
    // Wait for main thread to start experiment
    while (start.load() == 0) {
    }
    while (running.load() == 1) {
      if (rcv) {
        char* data = data1[id];
        int res = ff_recv(fd, data, DATA_LENGTH, MSG_WAITALL);
        std::atomic_ref<uint64_t>(bytes[id]).fetch_add(DATA_LENGTH);
        if (res != DATA_LENGTH) {
          if (running.load() == 1) {
            throw std::runtime_error("failed to receive");
          }
          done.fetch_add(1);
          return;
        }
      } else {
        ff_zc_mbuf *mbuf;
        if(ff_zc_mbuf_get(mbuf, DATA_LENGTH))
            throw std::runtime_error("failed to alloc mbuf");
        int res = ff_write(fd, mbuf, DATA_LENGTH);
        if (res != DATA_LENGTH) {
          if (running.load() == 1) {
            throw std::runtime_error("failed to send");
          }
          done.fetch_add(1);
          return;
        }
        std::atomic_ref<uint64_t>(bytes[id]).fetch_add(DATA_LENGTH);
      }
    }
    done.fetch_add(1);
  };
  if (dir == RECEIVE || dir == DUPLEX) {
    for (uint64_t t_id = 0; t_id < fds.size(); ++t_id) {
      thread_pool.emplace_back(do_work, t_id, true);
    }
  }
  if (dir == SEND || dir == DUPLEX) {
    for (uint64_t t_id = 0; t_id < fds.size(); ++t_id) {
      thread_pool.emplace_back(do_work, t_id, false);
    }
  }
  start.store(1);
  auto start_ts = std::chrono::system_clock::now();
  uint64_t total_bytes = 0;
  std::cout << "count,mbit" << std::endl;
  for (uint64_t count = 0; count < seconds; ++count) {
    sleep(1);
    uint64_t bytes_local = 0;
    for (uint64_t t_id = 0; t_id < fds.size(); ++t_id) {
      auto b = std::atomic_ref<uint64_t>(bytes[t_id]).exchange(0);
      bytes_local += b;
    }
    uint64_t mbit = bytes_to_mbit(bytes_local);
    total_bytes += bytes_local;
    std::cout << count << "," << mbit << std::endl;
  }
  running.store(0);
  for (auto fd : fds) {
    close(fd);
  }
  while (static_cast<size_t>(done.load()) != (dir == DUPLEX ? 2 * fds.size() : fds.size())) {
  }
  auto end_ts = std::chrono::system_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_ts - start_ts).count();
  std::cout << "data size [" << total_bytes << "] bytes" << std::endl;
  std::cout << "avg speed [" << bytes_to_mbit((total_bytes / duration) * 1'000) << "] Mbit/s"
            << std::endl;
  for (auto& t : thread_pool) {
    t.join();
  }
  for (auto* d : data1) {
    free(d);
  }
}

std::string direction_name_from_id(int direction_id) {
  if (direction_id == 0) {
    return "duplex";
  } else if (direction_id == 1) {
    return "receive";
  } else if (direction_id == 2) {
    return "send";
  } else {
    throw std::runtime_error("unsupported direction");
  }
}

std::pair<Direction, Direction> direction_from_id(int direction_id) {
  Direction server_dir = DUPLEX;
  Direction client_dir = DUPLEX;
  if (direction_id == 0) {
    server_dir = Direction::DUPLEX;
    client_dir = Direction::DUPLEX;
  } else if (direction_id == 1) {
    server_dir = Direction::SEND;
    client_dir = Direction::RECEIVE;
  } else if (direction_id == 2) {
    server_dir = Direction::RECEIVE;
    client_dir = Direction::SEND;
  } else {
    throw std::runtime_error("unsupported direction in dir from id");
  }
  return {client_dir, server_dir};
}

std::string benchmark_name_from_id(int id) {
  if (id == 1) {
    return "posix";
  } else if (id == 2) {
    return "uring";
  } else if (id == 3) {
    return "uring_zc";
  } else {
    throw std::runtime_error(fmt::format("illegal benchmark id: {}", id));
  }
}

struct Settings {
  uint64_t seconds{0};
  int connections{-1};
  int data_length{-1};
  int threads{-1};
  int direction_id{-1};
  int benchmark_id{-1};
  std::string to_string() {
    return fmt::format(
        "settings(seconds=[{}]-connections=[{}]-data_length=[{}]-threads=[{}]-client_direction=[{}]"
        "-benchmark=[{}]"
        ")",
        seconds, connections, data_length, threads, direction_name_from_id(direction_id),
        benchmark_name_from_id(benchmark_id));
  }
};

int main(int argc, char** argv) {
  ff_init(argc, argv);
  argc -= 2;
  argv += 2;
  int c;
  bool client = false;
  bool server = false;
  std::string server_ip;
  int connections = 1;
  uint64_t seconds = 5;
  int direction_id = -1;
  int benchmark_id = -1;
  while ((c = getopt(argc, argv, "sc:P:t:b:l:x:d:")) != -1) switch (c) {
      case 's':
        server = true;
        break;
      case 'c':
        client = true;
        server_ip = optarg;
        break;
      case 'P':
        connections = std::stoi(optarg);
        break;
      case 't':
        seconds = std::stoull(optarg);
        break;
      case 'd':
        direction_id = std::stoull(optarg);
        break;
      case 'b':
        benchmark_id = std::stoi(optarg);
        break;
      case 'l':
        DATA_LENGTH = std::stoi(optarg);
        break;
      case 'x':
        THREADS = std::stoi(optarg);
        break;
      default:
        break;
    }

  std::vector<int> fds;
  if (server) {
    int listen_fd = create_listen_fd(PORT);
    while (true) {
      std::cout << "---START---" << std::endl;
      int control_fd = accept(listen_fd);
      auto s = read<Settings>(control_fd);
      std::cout << s.to_string() << std::endl;
      auto [_, server_dir] = direction_from_id(s.direction_id);
      fds = accept_n_times(listen_fd, s.connections);
      benchmarker_mt(fds, server_dir, s.seconds);
      uint64_t report_size = read<uint64_t>(control_fd);
      std::string report;
      report.resize(report_size);
      recv(control_fd, report.data(), report.size(), MSG_WAITALL);
      std::cout << report;
      close(control_fd);
    }
  }
  if (client) {
    int control_fd = connect(server_ip, PORT);
    auto [client_dir, server_dir] = direction_from_id(direction_id);
    Settings s{seconds, connections, DATA_LENGTH, THREADS, direction_id, benchmark_id};
    std::cout << "sending experiment: " << s.to_string() << std::endl;
    write(control_fd, s);
    fds = connect_n_times(server_ip, PORT, connections);
    std::string report;
    auto setup = [&]() {
        return setup_fstack(connections, THREADS); 
    };
    auto setup_data = setup();
    ProcEvent e;
    {
      e.start_counters();
      if (benchmark_id == 1) {
        f_stack(fds, client_dir, setup_data);
      } 
      e.stop_counters();
      report = e.to_string();
    }
    for (auto fd : fds) {
      close(fd);
    }
    uint64_t report_size = report.size();
    write(control_fd, report_size);
    send(control_fd, report.data(), report.size(), MSG_WAITALL);
    close(control_fd);
  }

  return 0;
}
