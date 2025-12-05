
#pragma once

#include <stdexcept>
#include <unistd.h>

#include <cstdint>
#include <thread>
#include <vector>

#include "settings.hpp"
#include "utils.hpp"

#include "ff_api.h"

struct FStackSetupData : SetupData {};

inline std::unique_ptr<SetupData> setup_fstack(int conns, int threads) {
  if (conns % threads != 0) {
    throw std::runtime_error("conns must be multiple of threads");
  }
  FStackSetupData setup{};
  setup.threads = threads;
  setup.data1 = std::vector<char*>(conns, nullptr);
  for (int fd_id = 0; fd_id < conns; ++fd_id) {
    setup.data1[fd_id] = reinterpret_cast<char*>(malloc(DATA_LENGTH));
  }
  return std::make_unique<FStackSetupData>(std::move(setup));
}

inline void f_stack(std::vector<int>& fds, Direction dir, std::unique_ptr<SetupData>& s) {
  FStackSetupData* setup_data = reinterpret_cast<FStackSetupData*>(s.get());
  std::vector<std::thread> thread_pool;
  for (int t_id = 0; t_id < setup_data->threads; ++t_id) {
    thread_pool.emplace_back(
        [&](int id) {
          fd_set fdset1;
          fd_set fdset2;
          timeval timeout{0, 0};
          while (true) {
            FD_ZERO(&fdset1);
            FD_ZERO(&fdset2);
            int max_fd = fds[id];
            for (uint64_t fd_id = id; fd_id < fds.size(); fd_id += setup_data->threads) {
              FD_SET(fds[fd_id], &fdset1);
              FD_SET(fds[fd_id], &fdset2);
              max_fd = std::max(fds[fd_id], max_fd);
            }
            int ready_fds = 0;
            if (dir == Direction::RECEIVE) {
              ready_fds = ff_select(max_fd + 1, &fdset1, NULL, NULL, &timeout);
            } else if (dir == Direction::SEND) {
              ready_fds = ff_select(max_fd + 1, NULL, &fdset2, NULL, &timeout);
            } else if (dir == Direction::DUPLEX) {
              ready_fds = ff_select(max_fd + 1, &fdset1, &fdset2, NULL, &timeout);
            }
            if (ready_fds < 0) {
              return;
            }
            for (uint64_t fd_id = id; fd_id < fds.size(); fd_id += THREADS) {
              if (dir == Direction::RECEIVE || dir == Direction::DUPLEX) {
                if (FD_ISSET(fds[fd_id], &fdset1)) {
                  int res = ff_recv(fds[fd_id], setup_data->data1[fd_id], DATA_LENGTH, MSG_WAITALL);
                  if (res != DATA_LENGTH) {
                    return;
                  }
                }
              }
              if (dir == Direction::SEND || dir == Direction::DUPLEX) {
                if (FD_ISSET(fds[fd_id], &fdset2)) {
                  ff_zc_mbuf *mbuf;
                  if(ff_zc_mbuf_get(mbuf, DATA_LENGTH))
                      throw std::runtime_error("Failed to alloc mbuf");
                  int res = send(fds[fd_id], setup_data->data2[fd_id], DATA_LENGTH, 0);
                  if (res != DATA_LENGTH) {
                    return;
                  }
                }
              }
            }
          }
        },
        t_id);
  }
  for (auto& t : thread_pool) {
    t.join();
  }
  for (auto d : setup_data->data1) {
    free(d);
  }
  for (auto d : setup_data->data2) {
    free(d);
  }
}
