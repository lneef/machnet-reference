#include "kv.h"
#include "machnet_common.h"
#include <cassert>
#include <cstdint>
#include <deque>
#include <gflags/gflags.h>
#include <machnet.h>
#include <sys/types.h>

DEFINE_string(remote, "", "server ip");
DEFINE_string(local, "", "local ip");
DEFINE_uint32(lport, 1, "local port");
DEFINE_uint32(rport, 2, "remote port");

static constexpr unsigned kDefaultTXN = 1e6;
static std::random_device rdev;
static std::mt19937 rng{rdev()};
static std::uniform_int_distribution<int64_t> dist{INT64_MIN, INT64_MAX};
struct slot_storage {
  std::deque<unsigned> free_slots;
  std::vector<int64_t> elems;
  slot_storage(unsigned n) : elems(n) {
    for (unsigned i = 0; i < n; ++i)
      free_slots.push_back(i);
  }
};

static void handle_completion(kv::kv_packet<kv::kv_completion> *resp,
                              slot_storage &slt_storage) {
  slt_storage.free_slots.push_back(resp->id);
}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  int ret = machnet_init();
  assert(ret == 0 && "machnet init failed");

  MachnetFlow_t flow;
  slot_storage slt_storage{128};
  kv::kv_packet<kv::kv_request> req;
  kv::kv_packet<kv::kv_completion> resp;
  unsigned t = 0;
  unsigned c = 0;
  void *channel = machnet_attach();
  ret = machnet_connect(channel, FLAGS_local.c_str(), FLAGS_remote.c_str(),
                        FLAGS_rport, &flow);

  assert(ret == 0 && "Connect failed");

  ssize_t rcvd;
  while (t < kDefaultTXN) {
    while ((rcvd = machnet_recv(channel, &resp, sizeof(resp), &flow)) > 0) {
      handle_completion(&resp, slt_storage);
      ++c;
    }
    if (slt_storage.free_slots.empty())
      continue;
    auto tid = slt_storage.free_slots.front();
    slt_storage.free_slots.pop_front();
    kv::create_kv_request(reinterpret_cast<uint8_t *>(&req), tid, dist(rng));
    ret = machnet_send(channel, flow, &resp, sizeof(resp));
    ++t;
  }

  while (c < kDefaultTXN) {
    while ((rcvd = machnet_recv(channel, &resp, sizeof(resp), &flow)) > 0) {
      handle_completion(&resp, slt_storage);
      ++c;
    }
  }
  return 0;
}
