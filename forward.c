#include "ff_api.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_PKT_SIZE 1440

struct net_if {
  int sock_fd;
  size_t pkt_size;
};

int forward(void* arg) { 
    struct net_if *info = (struct net_if*)arg;
    int ret;
    size_t off;
    struct ff_zc_mbuf* pkt;

    ret = ff_zc_mbuf_get(pkt, info->pkt_size);
    if(ret < 0)
        return ret;
    ff_write(info->sock_fd, pkt->bsd_mbuf, info->pkt_size);
    return 0;
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  struct net_if info = {
      .pkt_size = 1400,
  };
  int ret;
  info.sock_fd = ff_socket(AF_INET, SOCK_STREAM, 0);
  if (info.sock_fd < 0) {
    return -1;
  }

  struct addrinfo hints = {
      .ai_family = AF_INET,
  };

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  do {
    ret =
        ff_connect(info.sock_fd, (struct linux_sockaddr *)&addr, sizeof(addr));
  } while (ret);

  ff_run(forward, &info);

  return 0;
}
