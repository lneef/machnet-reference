#include "kv.h"
#include "machnet_common.h"
#include <cassert>
#include <gflags/gflags.h>
#include <machnet.h>
#include <sys/types.h>
#include <signal.h>

static volatile int terminate = 0;

static void handler(int sig) {
  (void)sig;
  terminate = 1;
}

DEFINE_string(local, "", "Local IP address");
DEFINE_uint32(port, 2, "Port to listen");

int main(int argc, char **argv) {
    struct sigaction sa = {};
  sa.sa_handler = handler;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);  
  gflags::ParseCommandLineFlags(&argc, &argv, false);
  int ret = machnet_init();
  assert(ret == 0 && "machnet init failed");

  void *channel = machnet_attach();
  ret = machnet_listen(channel, FLAGS_local.c_str(), FLAGS_port);
  assert(ret == 0 && "Listen failed");

  kv::kv_packet<kv::kv_request> req;
  kv::kv_packet<kv::kv_completion> comp;
  MachnetFlow_t tx_flow, rx_flow;
  kv::kv_store store;
  store.prepare();

  while (!terminate) {
    const ssize_t rcvd = machnet_recv(channel, &req, sizeof(req), &rx_flow);
    assert(rcvd >= 0 && "recv failed");
    if (rcvd == 0)
      continue;
    store.serve(&comp, &req);
    tx_flow = rx_flow;
    do {
      ret = machnet_send(channel, tx_flow, &comp, sizeof(comp));
    } while (ret != 0);
  }

  return 0;
}
