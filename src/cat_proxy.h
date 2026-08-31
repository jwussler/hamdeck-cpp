#pragma once

// TCP CAT proxy: an external logger talks to the radio through this host.
//
// ⚠️ THIS IS WHAT REMOVES THE NEED FOR A VIRTUAL SERIAL-PORT SPLITTER. One
// program can hold a serial port. Without this, running N1MM alongside HamDeck
// means VSPE/VSPD splitting the port, which is a second thing to install, a
// second thing to configure, and a second thing to go wrong on a contest
// morning. Here both go through the poller's own queue instead.
//
//   N1MM: Configure Ports -> Port = TCP -> Host 127.0.0.1, Port 4532
//
// ⚠️ LOOPBACK ONLY, AND THAT IS THE WHOLE SECURITY MODEL. It forwards CAT
// verbatim, including TX1; - anything that can reach this port can key the
// transmitter. It must never be bound to 0.0.0.0, and it is not filtered,
// because a logger legitimately needs to key the rig for CW and PTT. If it ever
// needs to be reachable from another machine, that is a tunnel, not a bind.
//
// ⚠️ Commands are handed to the RadioPoller's task queue, never written to the
// port directly. Two writers on one serial line interleave, and the replies then
// land on whichever reader asked first - which is not a crash, it is a
// frequency readout that is quietly wrong.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

class RadioPoller;

class TcpCatProxy {
 public:
  TcpCatProxy(RadioPoller* rig, int port);

  // ⚠️ MUST stop and join. Destroying a joinable std::thread calls
  // std::terminate, which turned a clean "failed to bind, exit 1" into SIGABRT
  // once already in this codebase (see TxAudioReceiver).
  ~TcpCatProxy();

  // Binds and starts accepting. Returns false with `err` set; a proxy that
  // cannot start must not stop the host from running the radio.
  bool Start(std::string& err);
  void Stop();

  bool running() const { return running_.load(); }
  int port() const { return port_; }
  int clients() const { return clients_.load(); }
  long long commands() const { return commands_.load(); }

 private:
  void AcceptLoop();
  void HandleClient(int fd);

  // One CAT exchange through the poller's queue. Empty when the command is a
  // set, or when the rig did not answer.
  std::string Forward(const std::string& cmd);

  RadioPoller* rig_ = nullptr;
  int port_ = 0;
  int listen_fd_ = -1;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<int> clients_{0};
  std::atomic<long long> commands_{0};

  std::thread accept_thread_;
  std::vector<std::thread> client_threads_;
};
