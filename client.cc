#include "cpu.h"
#include "kv.h"
#include "machnet_common.h"
#include <cassert>
#include <cstdint>
#include <deque>
#include <gflags/gflags.h>
#include <iostream>
#include <machnet.h>
#include <pthread.h>
#include <random>
#include <thread>
#include <sys/types.h>
#include <hdr/hdr_histogram.h>

DEFINE_string(remote, "", "server ip");
DEFINE_string(local, "", "local ip");
DEFINE_uint32(rport, 2, "remote port");
DEFINE_uint64(duration, 2, "duration");
DEFINE_uint32(threads, 1, "threads");


static uint32_t kBufferSize = 256 * 1024;

struct slot_storage {
  struct slot{
      uint64_t ts;
      int64_t key;
  };  

  std::deque<unsigned> free_slots;
  std::vector<slot> elems;
  slot_storage(unsigned n) : elems(n) {
    for (unsigned i = 0; i < n; ++i)
      free_slots.push_back(i);
  }
};

static void handle_completion(kv::kv_packet<kv::kv_completion> *resp,
                              slot_storage &slt_storage) {
  slt_storage.free_slots.push_back(resp->id);
}

static int closed_fn(uint32_t tid, uint64_t duration) {
  set_thread_affinity(pthread_self(), tid);  
  std::vector<uint8_t> buffer(kBufferSize);
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<int64_t> dist(0, 1024 * 1024);
  hdr_histogram *hist;
  hdr_init(1, 500'000, 3, &hist);
  slot_storage slt_storage{128};
  kv::kv_packet<kv::kv_request> req;
  MachnetFlow_t rflow, flow;
  unsigned rcvd = 0;

  uint64_t rpcs_cnt = 0;
  uint64_t rpcs_finished = 0;
  uint64_t inflight = 0;
  auto ticks_per_us = get_tsc_freq() / 1e6;
  void* channel = machnet_attach();
  auto ret = machnet_connect(channel, FLAGS_local.c_str(), FLAGS_remote.c_str(),
                        FLAGS_rport, &flow);
  assert(ret == 0);
  auto rx_fn = [&] {
     auto now = rdtsc(); 
     while ((rcvd = machnet_recv(channel, buffer.data(), kBufferSize, &rflow)) > 0) {
      auto *resp = new (buffer.data()) kv::kv_packet<kv::kv_completion>;
      hdr_record_value(hist, (now - slt_storage.elems[resp->id].ts) / ticks_per_us);
        ++rpcs_cnt;
        --inflight;   
      handle_completion(resp, slt_storage);

    }  
  };
  auto start = rdtsc_precise();
  auto end = start + duration * get_tsc_freq();
  while (start < end) {
    rx_fn();  
    if (slt_storage.free_slots.empty())
      continue;
    auto sid = slt_storage.free_slots.front();
    slt_storage.free_slots.pop_front();
    auto &slt = slt_storage.elems[sid];
    slt.ts = rdtsc();
    slt.key = dist(rng);
    kv::create_kv_request(reinterpret_cast<uint8_t *>(&req), sid, slt.key);
    assert(req.id == sid);
    while(machnet_send(channel, flow, &req, sizeof(req)) < 0)
            rx_fn();
    ++inflight;
  }

  rpcs_finished = rpcs_cnt;
  while (inflight)
      rx_fn();

  std::cout << static_cast<double>(rpcs_finished) / (static_cast<double>(duration)) << std::endl;
  std::cout << hdr_value_at_percentile(hist, 99.0) << std::endl;
  return 0;
}


int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  init_tsc();
  int ret = machnet_init();
  assert(ret == 0 && "machnet init failed");
  std::vector<std::thread> threads;
  threads.reserve(FLAGS_threads);
  for (uint32_t i = 0; i < FLAGS_threads; ++i)
    threads.emplace_back(closed_fn, i, FLAGS_duration);
  for (auto &t : threads)
      t.join();
  return 0;
}
