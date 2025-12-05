
#pragma once

#include <liburing.h>
#include <liburing/io_uring.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "settings.hpp"
#include "utils.hpp"

struct UserData {
  int fd_id;
  bool rcv;
};
struct UringSetupData : SetupData {
  std::vector<UserData> udata1;
  std::vector<UserData> udata2;
  std::vector<struct io_uring> rings;
  int conns_per_thread;
};

inline std::unique_ptr<SetupData> setup_uring(int conns, int threads) {
  if (conns % threads != 0) {
    throw std::runtime_error("conns must be multiple of threads");
  }
  UringSetupData setup{};
  setup.threads = threads;
  setup.conns_per_thread = conns / threads;
  setup.rings = std::vector<struct io_uring>(threads);
  setup.data1 = std::vector<char*>(conns, nullptr);
  setup.udata1 = std::vector<UserData>(conns);
  setup.data2 = std::vector<char*>(conns, nullptr);
  setup.udata2 = std::vector<UserData>(conns);
  for (int fd_id = 0; fd_id < conns; ++fd_id) {
    setup.data1[fd_id] = reinterpret_cast<char*>(malloc(DATA_LENGTH));
    setup.udata1[fd_id] = UserData{fd_id, true};
    setup.data2[fd_id] = reinterpret_cast<char*>(malloc(DATA_LENGTH));
    setup.udata2[fd_id] = UserData{fd_id, false};
  }
  for (int t_id = 0; t_id < threads; ++t_id) {
    if (io_uring_queue_init(512, &setup.rings[t_id], IORING_SETUP_SQPOLL) < 0) {
      throw std::runtime_error("failed to initialize ring");
    }
  }
  return std::make_unique<UringSetupData>(std::move(setup));
}

inline void uring_poll(std::vector<int>& fds, Direction dir, std::unique_ptr<SetupData>& s) {
  UringSetupData* setup_data = reinterpret_cast<UringSetupData*>(s.get());
  std::vector<std::thread> thread_pool;
  for (int t_id = 0; t_id < setup_data->threads; ++t_id) {
    thread_pool.emplace_back(
        [&](int id) {
          struct io_uring* ring = &setup_data->rings[id];
          struct io_uring_sqe* sqe;
          struct io_uring_cqe* cqe;
          auto do_prep = [&](UserData* ud) {
            sqe = io_uring_get_sqe(ring);
            if (ud->rcv) {
              io_uring_prep_recv(sqe, fds[ud->fd_id], setup_data->data1[ud->fd_id], DATA_LENGTH,
                                 MSG_WAITALL);
            } else {
              io_uring_prep_send(sqe, fds[ud->fd_id], setup_data->data2[ud->fd_id], DATA_LENGTH,
                                 MSG_WAITALL);
            }
            sqe->user_data = reinterpret_cast<uint64_t>(ud);
            io_uring_submit(ring);
          };
          // Submit initial requests
          if (dir == RECEIVE || dir == DUPLEX) {
            for (int conn_nr = 0; conn_nr < setup_data->conns_per_thread; ++conn_nr) {
              int fd_id = conn_nr + (id * setup_data->conns_per_thread);
              do_prep(&setup_data->udata1[fd_id]);
            }
          }
          if (dir == SEND || dir == DUPLEX) {
            for (int conn_nr = 0; conn_nr < setup_data->conns_per_thread; ++conn_nr) {
              int fd_id = conn_nr + (id * setup_data->conns_per_thread);
              do_prep(&setup_data->udata2[fd_id]);
            }
          }
          while (true) {
            int err = io_uring_wait_cqe(ring, &cqe);
            if (err < 0 || cqe->res != DATA_LENGTH) {
              return;
            }
            do_prep(reinterpret_cast<UserData*>(cqe->user_data));
            io_uring_cqe_seen(ring, cqe);
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
  for (auto& r : setup_data->rings) {
    io_uring_queue_exit(&r);
  }
}

struct UringZCSetupData : UringSetupData {
  std::vector<struct iovec> iov;
};

inline std::unique_ptr<SetupData> setup_uring_zc(int conns, int threads) {
  if (conns % threads != 0) {
    throw std::runtime_error("conns must be multiple of threads");
  }
  UringZCSetupData setup{};
  setup.threads = threads;
  setup.conns_per_thread = conns / threads;
  setup.rings = std::vector<struct io_uring>(threads);
  setup.data1 = std::vector<char*>(conns, nullptr);
  setup.udata1 = std::vector<UserData>(conns);
  setup.data2 = std::vector<char*>(conns, nullptr);
  setup.udata2 = std::vector<UserData>(conns);
  setup.iov = std::vector<struct iovec>(conns);
  for (int fd_id = 0; fd_id < conns; ++fd_id) {
    setup.data1[fd_id] = reinterpret_cast<char*>(malloc(DATA_LENGTH));
    setup.udata1[fd_id] = UserData{fd_id, true};
    setup.data2[fd_id] = reinterpret_cast<char*>(malloc(DATA_LENGTH));
    setup.udata2[fd_id] = UserData{fd_id, false};
    setup.iov[fd_id].iov_base = setup.data2[fd_id];
    setup.iov[fd_id].iov_len = DATA_LENGTH;
  }
  for (int t_id = 0; t_id < threads; ++t_id) {
    if (io_uring_queue_init(512, &setup.rings[t_id], IORING_SETUP_SQPOLL) < 0) {
      throw std::runtime_error("failed to initialize ring");
    }
    if (io_uring_register_buffers(&setup.rings[t_id], &setup.iov[t_id * setup.conns_per_thread],
                                  setup.conns_per_thread) < 0) {
      throw std::runtime_error("failed to register buffers");
    }
  }
  return std::make_unique<UringZCSetupData>(std::move(setup));
}

inline void uring_zc(std::vector<int>& fds, Direction dir, std::unique_ptr<SetupData>& s) {
  UringZCSetupData* setup_data = reinterpret_cast<UringZCSetupData*>(s.get());
  std::vector<std::thread> thread_pool;
  for (int t_id = 0; t_id < setup_data->threads; ++t_id) {
    thread_pool.emplace_back(
        [&](int id) {
          struct io_uring* ring = &setup_data->rings[id];
          struct io_uring_sqe* sqe;
          struct io_uring_cqe* cqe;
          auto do_prep = [&](UserData* ud) {
            sqe = io_uring_get_sqe(ring);
            if (ud->rcv) {
              io_uring_prep_recv(sqe, fds[ud->fd_id], setup_data->data1[ud->fd_id], DATA_LENGTH,
                                 MSG_WAITALL);
            } else {
              io_uring_prep_send_zc_fixed(sqe, fds[ud->fd_id], setup_data->data2[ud->fd_id],
                                          DATA_LENGTH, MSG_WAITALL, 0,
                                          ud->fd_id - id * setup_data->conns_per_thread);
            }
            sqe->user_data = reinterpret_cast<uint64_t>(ud);
            io_uring_submit(ring);
          };
          // Submit initial requests
          if (dir == RECEIVE || dir == DUPLEX) {
            for (int conn_nr = 0; conn_nr < setup_data->conns_per_thread; ++conn_nr) {
              int fd_id = conn_nr + (id * setup_data->conns_per_thread);
              do_prep(&setup_data->udata1[fd_id]);
            }
          }
          if (dir == SEND || dir == DUPLEX) {
            for (int conn_nr = 0; conn_nr < setup_data->conns_per_thread; ++conn_nr) {
              int fd_id = conn_nr + (id * setup_data->conns_per_thread);
              do_prep(&setup_data->udata2[fd_id]);
            }
          }
          while (true) {
            int err = io_uring_wait_cqe(ring, &cqe);
            if (err < 0) {
              return;
            }
            if (cqe->flags & IORING_CQE_F_MORE) {
              io_uring_cqe_seen(ring, cqe);
              continue;
            }
            if (cqe->res != DATA_LENGTH) {
              return;
            }
            do_prep(reinterpret_cast<UserData*>(cqe->user_data));
            io_uring_cqe_seen(ring, cqe);
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
  for (auto& r : setup_data->rings) {
    io_uring_queue_exit(&r);
  }
}