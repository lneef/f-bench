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
#include <netdb.h>
#include <netinet/in.h>
#include <numeric>
#include <ostream>
#include <stddef.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>

#include <rte_eal.h>
#include <rte_lcore.h>

#include <atomic>
#include <thread>
#include <vector>

#include "PerfEvent.hpp"
#include "defs.h"

#include "ff_event.h"
#include "ff_api.h"
struct forward_settings {
  std::vector<int> fds;
};

sockaddr_in addr{};
uint64_t data_len = 64;
std::atomic<uint16_t> running{};
uint64_t seconds;
std::chrono::system_clock::time_point start, end, deadline;

struct thread_context{
    bool connectd;
    int kq;
    int sockfd;
    uint64_t pkts;
};

struct benchmark_context{
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

int forward(void* arg){
    auto *bc = static_cast<benchmark_context*>(arg);
    auto myid = rte_lcore_index(rte_lcore_id());
    auto *tc = &bc->threads[myid];

    if(!tc->connectd){
        auto myaddr = addr;
        if(ff_connect(tc->sockfd, (struct linux_sockaddr*)&myaddr, sizeof(myaddr)))
            return 0;
        tc->connectd = true;
        ++running;
    }else{
        ff_zc_mbuf *mbuf;
        ff_zc_mbuf_get(mbuf, data_len);
        if(ff_write(tc->sockfd, mbuf, data_len) < data_len)
            return -1;
        tc->pkts++;
    }

    if(rte_lcore_id() == 0){
        if(running == rte_lcore_count()){
            start = std::chrono::system_clock::now();
            deadline = start += std::chrono::seconds(seconds); 
            running = 0;
        }
        if(std::chrono::system_clock::now() >= deadline)
            ff_stop_run();
    }
    return 0;

}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  forward_settings info{};
  int opt;
  uint16_t connections, cores;
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
      data_len = std::atol(optarg);
      break;
    case 't':
      seconds = std::atol(optarg);
      break;
    case 'c':
      cores = std::atoi(optarg);
      break;
    }
  }
  std::vector<thread_context> tcs(rte_lcore_count());
  create_n_connections(info, rte_lcore_count());
  auto it = info.fds.begin();
  for(auto& tc: tcs){
      tc.sockfd = *(it++);
  }
  benchmark_context ctx;
  ctx.threads = std::move(tcs);
  PerfEvent event;
  {
    PerfEventBlock perf(event);
    ff_run(forward, &ctx);
  }
  end = std::chrono::system_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
          .count();
  uint64_t total = 0;
  for(auto& tc: tcs){
      total += tc.pkts;
  }
  auto out = std::format("Sent {} in {}ms - PPS: {:2}\n", total, duration,
                         static_cast<double>(total) / duration / 1000);
  std::cout << out;
  std::cout << std::flush;
  return 0;
}
