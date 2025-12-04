#include "ff_api.h"
#include <algorithm>
#include <bits/getopt_core.h>
#include <cstdint>
#include <cstdlib>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <aio.h>
#include <linux/aio_abi.h>
#include <getopt.h>
#include <arpa/inet.h>

#include <vector>

#include "defs.h"
#define MAX_PKT_SIZE 1440
struct net_if {
  int sock_fd;
  size_t pkt_size;
};

int forward(void* arg) { 

    struct net_if *info = (struct net_if*)arg;
    std::vector<ff_zc_mbuf*> sendbufs(PKT_BURST_SIZE, nullptr);
    std::vector<ff_zc_mbuf*> recvbufs(PKT_BURST_SIZE, nullptr);
    auto populate = [info](ff_zc_mbuf*& buf){
        ff_zc_mbuf_get(buf, info->pkt_size);
    };
    std::ranges::for_each_n(sendbufs.begin(), PKT_BURST_SIZE, populate);

    int ret;
    size_t off;
    struct ff_zc_mbuf* pkt;
    for(auto* pkt: sendbufs)
        ff_write(info->sock_fd, pkt->bsd_mbuf, info->pkt_size);
    return 0;
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  struct net_if info = {
      .pkt_size = 1400,
  };
  int opt;
  uint16_t port;
  struct sockaddr_in addr = {0};

  while((opt = getopt(argc - ARGS, argv + ARGS, "p:a:")) != -1){
      switch(opt){
          case 'p':
              addr.sin_port = htons(std::atoi(optarg));
              break;
        case 'a':
              addr.sin_addr.s_addr = inet_addr(optarg);
              break;
      }
  }
  int ret;
  info.sock_fd = ff_socket(AF_INET, SOCK_STREAM, 0);
  if (info.sock_fd < 0) {
    return -1;
  }

  addr.sin_family = AF_INET;
  do {
    ret =
        ff_connect(info.sock_fd, (struct linux_sockaddr *)&addr, sizeof(addr));
  } while (ret);

  ff_run(forward, &info);

  return 0;
}
