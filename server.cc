#include "kv.h"
#include "machnet_common.h"
#include <cassert>
#include <gflags/gflags.h>
#include <machnet.h>
#include <sys/types.h>

volatile int terminate = 0;

DEFINE_string(local, "", "Local IP address");
DEFINE_uint32(port, 2, "Port to listen");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  int ret = machnet_init();
  assert(ret == 0 && "machnet init failed");

  void *channel = machnet_attach();
  ret = machnet_listen(channel, FLAGS_local.c_str(), FLAGS_port);
  assert(ret == 0 && "Listen failed");

  kv::kv_packet<kv::kv_request> req;
  kv::kv_packet<kv::kv_completion> comp;
  MachnetFlow_t flow;
  kv::kv_store store;

  while (!terminate) {
    const ssize_t rcvd = machnet_recv(channel, &req, sizeof(req), &flow);
    if (rcvd == 0)
      continue;
    store.serve(&comp, &req);
    do {
      ret = machnet_send(channel, flow, &comp, sizeof(comp));
    } while (ret != 0);
  }

  return 0;
}
