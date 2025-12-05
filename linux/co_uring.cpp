#include <arpa/inet.h>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

constexpr int QUEUE_DEPTH = 64;
constexpr int BUFFER_SIZE = 1024;
constexpr double DEFAULT_BENCHMARK_DURATION = 5.0;
constexpr double EVENT_RATE = 10.0;  // 10 events per second

enum class OperationType { RECV = 1, SEND = 2 };

struct OperationData {
  OperationType op_type;
  sockaddr_in client_addr;
  socklen_t addrlen;
  uint32_t id;
  std::vector<char> buffer;
};

struct Uring {
  int sockfd;
  io_uring ring;
  std::vector<char> recv_buffer;
  sockaddr_in server_addr, client_addr;
  std::chrono::steady_clock::time_point start_time;
  std::vector<double> timings_vec;
  std::vector<double> response_times;
  std::map<uint32_t, std::chrono::steady_clock::time_point> send_times;
  double benchmark_duration;
  size_t timings_index = 0;

  void generate_timings_vec(double duration, double rate) {
    std::default_random_engine generator;
    std::exponential_distribution<double> distribution(rate);
    double sum = 0.0;
    while (sum < duration) {
      double interarrival = distribution(generator);
      sum += interarrival;
      if (sum < duration) {
        timings_vec.push_back(sum);
      }
    }
  }

  void setup(int port, std::string client_addr_str, int client_port, double duration) {
    benchmark_duration = duration;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
      throw std::runtime_error("Socket creation failed");
    }

    client_addr = {};
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    inet_pton(AF_INET, client_addr_str.c_str(), &(client_addr.sin_addr));

    server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      throw std::runtime_error("Bind failed");
    }

    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
      throw std::runtime_error("io_uring initialization failed");
    }

    generate_timings_vec(benchmark_duration, EVENT_RATE);
    recv_buffer.resize(BUFFER_SIZE);
  }

  void add_recv_request() {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    OperationData* op_data = new OperationData();
    op_data->op_type = OperationType::RECV;
    op_data->addrlen = sizeof(sockaddr_in);
    io_uring_prep_recv(sqe, sockfd, recv_buffer.data(), BUFFER_SIZE, 0);
    io_uring_sqe_set_data(sqe, op_data);
  }

  void add_send_request(uint32_t id) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    OperationData* op_data = new OperationData();
    op_data->op_type = OperationType::SEND;
    op_data->id = id;
    uint32_t net_id = htonl(id);
    op_data->buffer.resize(sizeof(net_id));
    memcpy(op_data->buffer.data(), &net_id, sizeof(net_id));
    io_uring_prep_sendto(sqe, sockfd, op_data->buffer.data(), op_data->buffer.size(), 0,
                         (struct sockaddr*)&client_addr, sizeof(client_addr));
    io_uring_sqe_set_data(sqe, op_data);
  }

  Uring() : sockfd(-1) {}

  ~Uring() {
    if (sockfd != -1) {
      close(sockfd);
    }
    io_uring_queue_exit(&ring);
  }

  void run(bool server_mode) {
    start_time = std::chrono::steady_clock::now();
    add_recv_request();

    while (true) {
      auto current_time = std::chrono::steady_clock::now();
      std::chrono::duration<double> elapsed = current_time - start_time;

      if (elapsed.count() >= benchmark_duration) {
        // Benchmark finished
        break;
      }

      // Check if it's time to send the next message
      while (timings_index < timings_vec.size() && elapsed.count() >= timings_vec[timings_index]) {
        // Send message with ID timings_index
        add_send_request(timings_index);
        // Record send time
        send_times[timings_index] = current_time;
        timings_index++;
      }

      io_uring_submit(&ring);

      io_uring_cqe* cqe;
      int ret = io_uring_wait_cqe(&ring, &cqe);

      if (ret == -ETIME) {
        continue;
      } else if (ret < 0) {
        std::cerr << "Error waiting for completion: " << strerror(-ret) << std::endl;
        continue;
      }

      OperationData* op_data = (OperationData*)io_uring_cqe_get_data(cqe);

      if (op_data->op_type == OperationType::RECV) {
        if (cqe->res > 0) {
          // Extract ID from recv_buffer
          uint32_t net_id;
          memcpy(&net_id, recv_buffer.data(), sizeof(net_id));
          uint32_t id = ntohl(net_id);
          auto recv_time = std::chrono::steady_clock::now();
          // Calculate response time
          double response_time =
              std::chrono::duration<double>(recv_time - start_time).count() - timings_vec[id];
          response_times.push_back(response_time);
        }
        // Queue up another receive
        add_recv_request();
      }

      delete op_data;
      io_uring_cqe_seen(&ring, cqe);
    }

    // After benchmark is done, compute statistics
    if (!response_times.empty()) {
      double sum = std::accumulate(response_times.begin(), response_times.end(), 0.0);
      double avg = sum / response_times.size();
      double min_val = *std::min_element(response_times.begin(), response_times.end());
      double max_val = *std::max_element(response_times.begin(), response_times.end());
      std::cout << "Average response time: " << avg << " seconds" << std::endl;
      std::cout << "Minimum response time: " << min_val << " seconds" << std::endl;
      std::cout << "Maximum response time: " << max_val << " seconds" << std::endl;
    } else {
      std::cout << "No responses received" << std::endl;
    }
  }

  void client_run(bool server_mode) {
    add_recv_request();

    while (true) {
      io_uring_submit(&ring);

      io_uring_cqe* cqe;
      int ret = io_uring_wait_cqe(&ring, &cqe);
      if (ret < 0) {
        std::cerr << "Error waiting for completion: " << strerror(-ret) << std::endl;
        continue;
      }

      OperationData* op_data = (OperationData*)io_uring_cqe_get_data(cqe);

      if (op_data->op_type == OperationType::RECV) {
        if (cqe->res > 0) {
          // Extract ID from recv_buffer
          uint32_t net_id;
          memcpy(&net_id, recv_buffer.data(), sizeof(net_id));
          uint32_t id = ntohl(net_id);
          // Send back to the server
          add_client_send_request(op_data->client_addr, id);
        }
        // Queue up another receive
        add_recv_request();
      }

      delete op_data;
      io_uring_cqe_seen(&ring, cqe);
    }
  }

  void add_client_send_request(sockaddr_in dest_addr, uint32_t id) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    OperationData* op_data = new OperationData();
    op_data->op_type = OperationType::SEND;
    op_data->client_addr = dest_addr;
    uint32_t net_id = htonl(id);
    op_data->buffer.resize(sizeof(net_id));
    memcpy(op_data->buffer.data(), &net_id, sizeof(net_id));
    io_uring_prep_sendto(sqe, sockfd, op_data->buffer.data(), op_data->buffer.size(), 0,
                         (struct sockaddr*)&(op_data->client_addr), sizeof(op_data->client_addr));
    io_uring_sqe_set_data(sqe, op_data);
  }
};

int main(int argc, char** argv) {
  int opt;
  bool server = false;
  int port = 12345;
  int client_port = 12346;
  std::string client_addr_str = "127.0.0.1";
  double benchmark_duration = DEFAULT_BENCHMARK_DURATION;

  while ((opt = getopt(argc, argv, "sp:c:a:t:")) != -1) {
    switch (opt) {
      case 's':
        server = true;
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 'c':
        client_addr_str = optarg;
        break;
      case 'a':
        client_port = atoi(optarg);
        break;
      case 't':
        benchmark_duration = atof(optarg);
        break;
    }
  }

  Uring ring;
  ring.setup(port, client_addr_str, client_port, benchmark_duration);

  if (server) {
    ring.run(server);
  } else {
    ring.client_run(server);
  }

  return 0;
}
