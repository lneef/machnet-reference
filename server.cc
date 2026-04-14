#include "bench.h"
#include "cpu.h"
#include "kv.h"
#include "machnet_common.h"
#include <cassert>
#include <cstdint>
#include <gflags/gflags.h>
#include <machnet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <ranges>
#include <string_view>
#include <thread>

DEFINE_string(local, "", "Local IP address");
DEFINE_string(ports, "2", "Port to listen");
DEFINE_uint32(len, 8, "Len");
DEFINE_uint32(threads, 1, "Threads");

static uint32_t kBufferSize = 256 * 1024;
static volatile int terminate = 0;
static bench::storage store;

static void handler(int sig) {
  (void)sig;
  terminate = 1;
}

void serve(std::span<uint8_t> sbuffer, kv::kv_packet<kv::kv_request> *req,
           size_t &len) {
  auto *resp = new (sbuffer.data()) kv::kv_packet<kv::kv_completion>;
  len = sizeof(*resp);
  auto key = req->payload.key;
  auto it = store.find(key);
  resp->id = req->id;
  resp->pt = req->pt;
  resp->payload.key = req->payload.key;
  if (it == store.end()) {
    resp->payload.reponse = kv::response_t::FAILURE;
    resp->payload.data_len = 0;
  } else {
    resp->payload.reponse = kv::response_t::SUCCESS;
    resp->payload.data_len = it->second.size();
    std::memcpy(resp->payload.data, it->second.data(), it->second.size());
    len += resp->payload.data_len;
  }
}

void server_fun(unsigned tid, uint16_t port) {
 set_thread_affinity(pthread_self(), tid);   
  int ret = 0;
  void *channel = machnet_attach();

  ret = machnet_listen(channel, FLAGS_local.c_str(), port);
  assert(ret == 0 && "Listen failed");
  std::vector<uint8_t> rbuffer(kBufferSize);
  std::vector<uint8_t> sbuffer(kBufferSize);
  MachnetFlow_t tx_flow, rx_flow;
  while (!terminate) {
    size_t resp_len = 0;
    const ssize_t rcvd =
        machnet_recv(channel, rbuffer.data(), kBufferSize, &rx_flow);
    assert(rcvd >= 0 && "recv failed");
    if (rcvd == 0)
      continue;
    tx_flow.dst_ip = rx_flow.src_ip;
    tx_flow.src_ip = rx_flow.dst_ip;
    tx_flow.dst_port = rx_flow.src_port;
    tx_flow.src_port = rx_flow.dst_port;
    serve(rbuffer, new (rbuffer.data()) kv::kv_packet<kv::kv_request>,
          resp_len);
    do {
      ret = machnet_send(channel, tx_flow, sbuffer.data(), resp_len);
    } while (ret != 0);
  }
}

int main(int argc, char **argv) {
  struct sigaction sa = {};
  sa.sa_handler = handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  gflags::ParseCommandLineFlags(&argc, &argv, false);

  bench::prepare(store, FLAGS_len);
  int ret = machnet_init();
  std::vector<uint16_t> ports;
  assert(ret == 0 && "machnet init failed");
  for (auto part : std::string_view(FLAGS_ports) | std::views::split(':'))
    ports.push_back(static_cast<uint16_t>(
        std::atoi(std::string(part.begin(), part.end()).c_str())));
  std::vector<std::thread> threads;
  threads.reserve(FLAGS_threads);
  for (auto i = 0u; i < FLAGS_threads; ++i)
    threads.emplace_back(server_fun, i, ports[i % ports.size()]);
  for (auto &t : threads)
      t.join();

  return 0;
}
