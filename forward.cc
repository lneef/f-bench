#include "ff_api.h"
#include <aio.h>
#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <getopt.h>
#include <iostream>
#include <linux/aio_abi.h>
#include <netdb.h>
#include <netinet/in.h>
#include <numeric>
#include <ostream>
#include <stddef.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>

#include <atomic>
#include <thread>
#include <vector>

#include "PerfEvent.hpp"
#include "defs.h"
struct forward_settings {
  size_t pkt_size;
  struct sockaddr_in addr;
  std::atomic<bool> running;
  std::vector<uint64_t> pkts;
  std::vector<int> fds;
};

static void create_n_connections(forward_settings &info, unsigned int n) {
  for (auto i = 0u; i < n; ++i) {
    int sock_fd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
      throw std::runtime_error(
          std::format("Failed to open socket: {}\n", strerror(errno)));
    if (ff_connect(sock_fd, reinterpret_cast<linux_sockaddr *>(&info.addr),
                   sizeof(info.addr)))
      throw std::runtime_error(
          std::format("Failed to connect to server: {}\n", strerror(errno)));
    info.fds.push_back(sock_fd);
  }
}

void forward(forward_settings &info, int tid) {
  auto peer = info.fds[tid];
  uint64_t pkts = 0;
  ff_zc_mbuf *mbuf;
  while (!info.running.load())
    ;
  while (info.running.load()) {
    if (ff_zc_mbuf_get(mbuf, info.pkt_size))
      return;
    if (ff_write(peer, mbuf, info.pkt_size) < info.pkt_size)
      return;
    ++pkts;
  }
  info.pkts[tid] = pkts;
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  forward_settings info{};
  int opt;
  uint16_t port;
  uint16_t connections, cores, seconds;
  auto &addr = info.addr;
  addr.sin_family = AF_INET;
  while ((opt = getopt(argc - ARGS, argv + ARGS, "p:a:s:t:n:c:")) != -1) {
    switch (opt) {
    case 'p':
      addr.sin_port = htons(std::atoi(optarg));
      break;
    case 'a':
      addr.sin_addr.s_addr = inet_addr(optarg);
      break;
    case 's':
      info.pkt_size = std::atol(optarg);
      break;
    case 't':
      seconds = std::atol(optarg);
      break;
    case 'n':
      connections = std::atoi(optarg);
      break;
    case 'c':
      cores = std::atoi(optarg);
      break;
    }
  }

  uint16_t i = 0;
  std::vector<int> cons(connections);
  info.pkts.resize(cores, 0);
  create_n_connections(info, connections);
  std::vector<std::thread> threads(cores);
  PerfEvent event;
  for (auto &t : threads) {
    t = std::thread(forward, std::ref(info), cons[i++]);
  }

    auto start = std::chrono::system_clock::now();
  {
    PerfEventBlock perf(event);
    info.running.store(true);
    sleep(seconds);
    for (auto &t : threads) {
      t.join();
    }
  }

    auto end = std::chrono::system_clock::now();
  auto total = std::reduce(info.pkts.begin(), info.pkts.end());
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  auto out = std::format("Sent {} in {}ms - PPS: {:2}\n", total, duration,
                         static_cast<double>(total) / duration / 1000);
  std::cout << out;
  std::cout << std::flush;
  return 0;
}
