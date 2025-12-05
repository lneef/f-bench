#include "defs.h"
#include "ff_api.h"
#include <algorithm>
#include <arpa/inet.h>
#include <asm-generic/ioctls.h>
#include <atomic>
#include <bits/getopt_core.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <ranges>
#include <rte_lcore.h>
#include <stddef.h>
#include <stdexcept>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <vector>
#include <iostream>

#define MAX_EVENTS 512 

static uint64_t pkt_size = 64;
static uint16_t PORT = 30000;

struct receiver_context {
  int kq;
  struct kevent kevSet;
  struct kevent events[MAX_EVENTS];
  std::vector<char> buf;
};

using benchmark_context = std::vector<receiver_context>;

struct server_context {
  int sockfd;
  int kq;
  struct kevent kevSet;
  struct kevent events[MAX_EVENTS];
  std::vector<int> clients;
};

static int create_server_socket(struct sockaddr_in &addr) {
  int sockfd = ff_socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
    throw std::runtime_error(
        std::format("Socket creation failed with: {}\n", strerror(errno)));
  if (ff_bind(sockfd, (struct linux_sockaddr *)&addr, sizeof(addr)))
    throw std::runtime_error(
        std::format("Socket creation failed with: {}\n", strerror(errno)));
  if (ff_listen(sockfd, rte_lcore_count()))
    throw std::runtime_error(
        std::format("Socket creation failed with: {}\n", strerror(errno)));
  return sockfd;
}

static int accept_n_connections(void *arg) {
  auto *sc = static_cast<server_context *>(arg);
  if (rte_lcore_index(rte_lcore_id()) != 0)
    return 0;
  int nevents = ff_kevent(sc->kq, NULL, 0, sc->events, MAX_EVENTS, NULL);
  for (auto &event : std::ranges::subrange(sc->events, nevents)) {
    auto clientfd = static_cast<int>(event.ident);
    if (clientfd == sc->sockfd) {
      int available = (int)event.data;
      do {
        int nclientfd = ff_accept(clientfd, NULL, NULL);
        if (nclientfd < 0) {
          break;
        }
        available--;
      } while (available);
    }
    if (sc->clients.size() >= rte_lcore_count())
      ff_stop_run();
  }
  return 0;
}

int receiver_fn(void *arg) {
  auto &bc = *static_cast<benchmark_context *>(arg);
  auto &rc = bc[rte_lcore_index(rte_lcore_id())];
  int nevents = ff_kevent(rc.kq, NULL, 0, rc.events, MAX_EVENTS, NULL);
  for (auto &event : std::ranges::subrange(rc.events, nevents)) {
    auto clientfd = event.ident;
    if (event.filter == EVFILT_READ) {
      (void)ff_recv(clientfd, rc.buf.data(), rc.buf.size(), MSG_WAITALL);

    }
  }
  return 0;
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  int opt;
  struct sockaddr_in addr = {0};
  addr.sin_port = htons(PORT);
  while ((opt = getopt(argc - ARGS, argv + ARGS, "p:s:")) != -1) {
    switch (opt) {
    case 'p':
      addr.sin_port = htons(std::atoi(optarg));
      break;
    case 's':
      pkt_size = std::atoi(optarg);
      break;
    }
  }

  server_context sc;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  sc.sockfd = create_server_socket(addr);
  int on = 1;
  ff_setsockopt(sc.sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  std::cout << "Accepting connections on " << ntohs(addr.sin_port) << std::endl;

  ff_run(accept_n_connections, &sc);
  benchmark_context rcs(rte_lcore_count());
  auto it = sc.clients.begin();
  for (auto &rc : rcs) {
    rc.kq = ff_kqueue();
    EV_SET(&rc.kevSet, *(it++), EVFILT_READ, EV_ADD, 0, MAX_EVENTS, NULL);
    ff_kevent(rc.kq, &rc.kevSet, 1, NULL, 0, NULL);
  }
  ff_run(receiver_fn, &rcs);
  for(auto cl : sc.clients)
      close(cl);
  ff_close(sc.sockfd);
  return 0;
}
