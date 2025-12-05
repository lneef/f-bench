#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <fmt/core.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

enum Direction { SEND, RECEIVE, DUPLEX };

struct SetupData {
  std::vector<char*> data1;
  std::vector<char*> data2;
  int conns;
  int threads;
};

template <class T>
inline T read(int fd) {
  T t{};
  if (recv(fd, reinterpret_cast<char*>(&t), sizeof(T), MSG_WAITALL) != sizeof(T)) {
    throw std::runtime_error("failed recv");
  }
  return t;
}

template <class T>
inline void write(int fd, T t) {
  if (send(fd, reinterpret_cast<char*>(&t), sizeof(T), 0) != sizeof(T)) {
    throw std::runtime_error("failed send");
  }
}

inline std::vector<int> accept_n_times(int listen_fd, uint64_t num_conn) {
  std::vector<int> conns;
  sockaddr_in client_addr;
  for (uint64_t idx = 0; idx < num_conn; ++idx) {
    socklen_t addr_len = sizeof(sockaddr_storage);
    auto read_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    conns.push_back(read_fd);
  }
  return conns;
}

inline int accept(int listen_fd) {
  auto conns = accept_n_times(listen_fd, 1);
  return conns[0];
}

inline int create_listen_fd(int port) {
  int _listen_fd = -1;
  _listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in server_addr {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);
  int on = 1;
  setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  int err = bind(_listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if (err < 0) {
    throw std::runtime_error(fmt::format("Failed to bind: {}", strerror(errno)));
  }

  err = ::listen(_listen_fd, 1024);
  if (err < 0) {
    throw std::runtime_error(fmt::format("Failed to listen: {}", strerror(errno)));
  }
  std::signal(SIGPIPE, SIG_IGN);
  return _listen_fd;
}

inline std::vector<int> connect_n_times(std::string host, int port, uint64_t num_conn) {
  std::vector<int> conns;
  for (uint64_t idx = 0; idx < num_conn; ++idx) {
    int _send_fd = -1;
    _send_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
    int on = 1;
    setsockopt(_send_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    int err = -1;
    do {
      err = connect(_send_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    } while (err < 0);
    std::signal(SIGPIPE, SIG_IGN);
    conns.push_back(_send_fd);
  }
  return conns;
}


inline int connect(std::string host, int port) { return connect_n_times(host, port, 1)[0]; }

inline uint64_t bytes_to_mbit(uint64_t bytes) { return (bytes * 8) / 1'000'000; }
