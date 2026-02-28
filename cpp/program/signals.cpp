#include "signals.h"
#include <csignal>

namespace Signals {
  std::atomic<bool> sigReceived(false);
  std::atomic<bool> shouldStop(false);

  void signalHandler(const int signal) {
    if(signal == SIGINT || signal == SIGTERM) {
      sigReceived.store(true);
      shouldStop.store(true);
    }
  }
}
