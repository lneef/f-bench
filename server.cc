#include "defs.h"
#include "ff_api.h"
#include <arpa/inet.h>
#include <asm-generic/ioctls.h>
#include <atomic>
#include <bits/getopt_core.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdexcept>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <vector>
#define MAX_PKT_SIZE 1440
#define MAX_EVENTS 128

struct server_settings {
  int sockfd;  
  size_t pkt_size;
  struct sockaddr_in addr;
  std::atomic<bool> running;
  std::vector<uint64_t> pkts;
  std::vector<int> fds;
};
static int create_server_socket(struct sockaddr_in& addr, unsigned int n){
    int sockfd = ff_socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
        throw std::runtime_error(std::format("Socket creation failed with: {}\n", strerror(errno)));
    if(ff_bind(sockfd, (struct linux_sockaddr *)&addr, sizeof(addr)))
        throw std::runtime_error(std::format("Socket creation failed with: {}\n", strerror(errno)));
    if(ff_listen(sockfd, n))
        throw std::runtime_error(std::format("Socket creation failed with: {}\n", strerror(errno)));
    return sockfd;
}

static void accept_n_connections(server_settings &info, unsigned int n,
                                 std::vector<int> &con) {
  for (auto i = 0u; i < n; ++i) {
    int client = ff_accept(info.sockfd, nullptr, nullptr);
    con.push_back(client);
  }
}

void server_fn(server_settings &info, int tid) {
  std::vector<std::byte> buf(info.pkt_size);
  uint64_t rcvd = 0;
  int64_t read;
  info.running.wait(false);
  while (info.running) {
    read = ff_read(info.fds[tid], buf.data(), info.pkt_size);
    rcvd += read == info.pkt_size;
    if(read < 0)
        break;
  }
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  server_settings info = {};
  int ret, opt;
  unsigned int num_con = 1;
  struct sockaddr_in addr = {0};

  while ((opt = getopt(argc - ARGS, argv + ARGS, "p:n:s:")) != -1) {
    switch (opt) {
    case 'p':
      addr.sin_port = htons(std::atoi(optarg));
      break;
    case 'n':
      num_con = std::atoi(optarg);
      break;
    case 's':
      info.pkt_size = std::atoi(optarg);
      break;
    }
  }
  info.sockfd = ff_socket(AF_INET, SOCK_STREAM, 0);
  if (info.sockfd < 0) {
    return -1;
  }
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  info.sockfd = create_server_socket(addr, num_con);
  int on = 1;
  unsigned int tn = 0;
  ff_setsockopt(info.sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  std::vector<int> con;
  std::vector<std::thread> threads(num_con);
  con.reserve(num_con);
  accept_n_connections(info, num_con, con);
  for (auto &t : threads) {
    t = std::thread(server_fn, con[tn++], std::ref(info));
  }
  info.running.store(true);
  info.running.notify_all();
  for (auto &t : threads)
    t.join();
  ff_close(info.sockfd);
  return 0;
}
