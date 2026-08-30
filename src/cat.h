#pragma once

// CAT transport abstraction.
//
// Two backends will implement this: a real serial port (/dev/ttyUSB0 @ 38400) and
// a simulator. The simulator is not a toy - it is what lets ~all 141 routes be
// built and regression-tested while the radio stays attached to the VM and the
// station stays on the air. Assume it will outlive the port and be the thing CI
// runs against, because CI will never have a radio.
//
// ⚠️ The serial lock is NOT re-entrant across threads (CARRYOVER.md section 5).
// Nothing on a request thread may call into this. Only the poller does.

#include <optional>
#include <string>

class CatTransport {
 public:
  virtual ~CatTransport() = default;

  // Send one CAT command (including its terminating ';') and read the reply.
  // Returns nullopt on timeout or transport error - never a fabricated value.
  // A missing answer must stay visibly missing; that is the whole lesson of
  // /api/record/start reporting success for something it never did.
  virtual std::optional<std::string> Exchange(const std::string& cmd) = 0;

  // Send a command that the rig does not answer.
  virtual bool Send(const std::string& cmd) = 0;

  virtual bool Connected() const = 0;

  // Human-readable backend name, surfaced so nobody has to guess whether they
  // are looking at a real radio or the simulator.
  virtual std::string Describe() const = 0;
};
