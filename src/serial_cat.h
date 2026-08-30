#pragma once

// Real CAT over a serial port.
//
// Everything here is written and tested WITHOUT the radio: the tests drive it
// through a pty with a fake rig on the far end. The point is that the hardware
// window is spent measuring things only the radio can show - buffer depth, PTT
// tail, audio - rather than debugging a fixed-width field offset that a pty
// would have caught for free.
//
// ⚠️ Only ONE thread may ever call this. The serial lock is not re-entrant
// across threads (CARRYOVER.md section 5); RadioPoller owns it and request
// threads queue commands instead.

#include <string>
#include <vector>

#include "cat.h"

class SerialCat : public CatTransport {
 public:
  ~SerialCat() override;

  // Opens `device` at `baud`. Returns false and leaves the object closed on
  // failure - it never half-opens and reports success.
  bool Open(const std::string& device, int baud = 38400);

  // Tries each candidate and keeps the first that answers the identity probe.
  //
  // ⚠️ The probe is `ID;` and ONLY `ID;`. CARRYOVER.md section 9: probing with a
  // control route once changed the operating mode mid-session. This also matters
  // because the CP2105 is a DUAL UART - one physical device enumerates two
  // serial ports and only one of them is CAT - so the port has to be identified
  // by asking, not by assuming a number that shifts when USB devices come and go.
  bool OpenFirstResponding(const std::vector<std::string>& candidates, int baud = 38400);

  std::optional<std::string> Exchange(const std::string& cmd) override;
  bool Send(const std::string& cmd) override;
  bool Connected() const override { return fd_ >= 0; }
  std::string Describe() const override;

  void Close();

  const std::string& device() const { return device_; }

  // A reply that never arrives must time out, not block. A wedged read would
  // stall the poller, and the cache would go stale while the API kept answering.
  static constexpr int kReplyTimeoutMs = 250;
  // The FTDX-101MP answers ID; with this.
  static constexpr const char* kExpectedId = "ID0682;";

 private:
  bool ConfigurePort(int baud);
  int fd_ = -1;
  std::string device_;
  std::string id_;
};
