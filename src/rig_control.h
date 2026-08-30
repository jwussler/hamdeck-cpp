#pragma once

// How a background job drives the radio.
//
// ⚠️ CALLBACKS, NOT A RadioPoller POINTER. The tuners run on their own threads
// and must never touch the serial port: one thread owns it (WIP §4). The
// setters here QUEUE a CAT command and return; the getters read the poller's
// cache, which is at most one 200 ms cycle old. Passing the poller itself would
// make it far too easy to call something that writes to the port from the wrong
// thread, and the failure mode is interleaved replies attributed to the wrong
// command - individually plausible, and a frequency that is really the mode.

#include <functional>
#include <string>

struct RigControl {
  std::function<int()> get_power;
  std::function<std::string()> get_mode;
  std::function<void(int)> set_power;
  std::function<void(const std::string&)> set_mode;
  std::function<void(bool)> set_ptt;
};
