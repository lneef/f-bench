#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <ostream>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cassert>

#include <rte_eal.h>
#include <rte_lcore.h>

#include <atomic>
#include <vector>

#include "PerfEvent.hpp"
#include "defs.h"

#include "ff_api.h"
#include "ff_event.h"
struct forward_settings {
  std::vector<int> fds;
};

sockaddr_in addr{};
uint64_t data_len = 64;
std::atomic<uint16_t> running{};
uint64_t seconds;
std::chrono::system_clock::time_point start, end, deadline;

struct thread_context {
  bool connectd;
  int kq;
  struct kevent kevSet;
  struct kevent events[MAX_EVENTS];
  int sockfd;
  uint64_t pkts;
};

struct benchmark_context {
  std::vector<thread_context> threads;
};

static void create_n_connections(forward_settings &info, unsigned int n) {
  for (auto i = 0u; i < n; ++i) {
    int sock_fd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
      throw std::runtime_error(
          std::format("Failed to open socket: {}\n", strerror(errno)));
    info.fds.push_back(sock_fd);
  }
}

int connect_loop(void *arg) {
  auto *bc = static_cast<benchmark_context *>(arg);
  auto myid = rte_lcore_index(rte_lcore_id());
  auto *tc = &bc->threads[myid];
  if (!tc->connectd) {
    auto myaddr = addr;
    if (ff_connect(tc->sockfd, (struct linux_sockaddr *)&myaddr,
                   sizeof(myaddr))) {
      if (errno == EINPROGRESS) {
        int nevents = ff_kevent(tc->kq, NULL, 0, tc->events, 1, NULL);
        if (nevents == 1) {
          auto &event = tc->events[0];
          if (event.filter != EVFILT_WRITE)
            return -1;
        }

      } else {
        return -1;
      }
    }
    tc->connectd = true;
    ++running;
  }
  if (rte_lcore_index(rte_lcore_id()) == 0)
    if (running == rte_lcore_count())
      ff_stop_run();
  return 0;
}

int forward(void *arg) {
  auto *bc = static_cast<benchmark_context *>(arg);
  auto myid = rte_lcore_index(rte_lcore_id());
  auto *tc = &bc->threads[myid];
  ff_zc_mbuf mbuf;
  ff_zc_mbuf_get(&mbuf, data_len);
  if (ff_write(tc->sockfd, mbuf.bsd_mbuf, data_len) < data_len)
    return -1;
  tc->pkts++;

  if(rte_lcore_index(rte_lcore_id()) == 0)
    if (std::chrono::system_clock::now() >= deadline)
        ff_stop_run();

  return 0;
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  forward_settings info{};
  int opt;
  addr.sin_family = AF_INET;
  while ((opt = getopt(argc, argv, "p:a:s:t:n:")) != -1) {
    switch (opt) {
    case 'p':
      addr.sin_port = htons(std::atoi(optarg));
      break;
    case 'a':
      addr.sin_addr.s_addr = inet_addr(optarg);
      break;
    case 's':
      data_len = std::atol(optarg);
      break;
    case 't':
      seconds = std::atol(optarg);
      break;
    }
  }
  std::vector<thread_context> tcs(rte_lcore_count());
  create_n_connections(info, rte_lcore_count());
  auto it = info.fds.begin();
  int on = 1;
  for (auto &tc : tcs) {
    tc.sockfd = *(it++);
    assert(tc.sockfd > 0);
    ff_ioctl(tc.sockfd, FIONBIO, &on);
    tc.kq = ff_kqueue();
    EV_SET(&tc.kevSet, tc.sockfd, EVFILT_WRITE, EV_ADD, 0, MAX_EVENTS, NULL);
    ff_kevent(tc.kq, &tc.kevSet, 1, NULL, 0, NULL);
  }
  benchmark_context ctx;
  ctx.threads = std::move(tcs);
  ff_run(connect_loop, &ctx);

  PerfEvent event;
  start = std::chrono::system_clock::now();
  {
    PerfEventBlock perf(event);
    ff_run(forward, &ctx);
  }
  end = std::chrono::system_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  uint64_t total = 0;
  for (auto &tc : tcs) {
    total += tc.pkts;
  }
  auto out = std::format("Sent {} in {}ms - PPS: {:2}\n", total, duration,
                         static_cast<double>(total) / duration / 1000);
  std::cout << out;
  std::cout << std::flush;
  return 0;
}
