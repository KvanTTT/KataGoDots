#ifndef PROGRAM_SIGNALS_H_
#define PROGRAM_SIGNALS_H_

#include <atomic>

namespace Signals {
  extern std::atomic<bool> sigReceived;
  extern std::atomic<bool> shouldStop;

  void signalHandler(int signal);
}

#endif // PROGRAM_SIGNALS_H_
