#include "ff_api.h"
#include "ff_event.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <cstdlib>

#include "defs.h"
#define MAX_PKT_SIZE 1440
#define MAX_BACKLOG 16
#define MAX_EVENTS 128

struct net_if {
  int sockfd;
  size_t pkt_size;
  int kq;
  uint64_t rcvd;
  struct kevent kevSet;
  struct kevent events[MAX_EVENTS];
};

static char buf[MAX_PKT_SIZE];

int receive(void *arg) {
  struct net_if *info = (struct net_if *)arg;
  unsigned nevents = ff_kevent(info->kq, NULL, 0, info->events, MAX_EVENTS, 0);
  unsigned i = 0;
  for(; i < nevents; ++i){
      int client_fd = info->events[i].ident;
      if(info->events[i].fflags & EV_EOF){
          ff_close(info->events[i].ident);
      }else if(client_fd == info->sockfd){
            int available = (int)info->events[i].data;
            do {
                int nclient_fd = ff_accept(client_fd, NULL, NULL);
                if (nclient_fd < 0) {
                    break;
                }
                EV_SET(&info->kevSet, nclient_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);

                if(ff_kevent(info->kq, &info->kevSet, 1, NULL, 0, NULL) < 0) 
                    return -1;
                available--;
            } while (available);
      
      }else if (info->events[i].fflags & EVFILT_READ){
          ff_read(client_fd, buf, sizeof(buf));
          ++info->rcvd;

      }
  }
  return 0;
}

int main(int argc, char **argv) {
  ff_init(argc, argv);
  struct net_if info = {
      .pkt_size = 1400,
  };
  int ret, opt;
  struct sockaddr_in addr = {0};

  while((opt = getopt(argc - ARGS, argv + ARGS, "p:")) != -1){
      switch(opt){
          case 'p':
              addr.sin_port = htons(std::atoi(optarg));
              break;
      }
  }
  info.sockfd = ff_socket(AF_INET, SOCK_STREAM, 0);
  if (info.sockfd < 0) {
    return -1;
  }

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ret = ff_bind(info.sockfd, (struct linux_sockaddr *)&addr, sizeof(addr));

  ff_listen(info.sockfd, MAX_BACKLOG);
  EV_SET(&info.kevSet, info.sockfd, EVFILT_READ, EV_ADD, 0, MAX_EVENTS, NULL);
  ff_kevent(info.kq, &info.kevSet, 1, NULL, 0, NULL);
  ff_run(receive, &info);
  ff_close(info.sockfd);
  return 0;
}
