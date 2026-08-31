#include "cat_proxy.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <future>
#include <memory>
#include <set>

#include "cat.h"
#include "log.h"
#include "radio.h"

namespace {

// ⚠️ QUERIES ONLY. A Yaesu "set" produces no reply, so waiting for one blocks
// until the read times out - and with a logger polling the frequency several
// times a second, that stalls the whole CAT queue behind commands that were
// never going to answer.
//
// Copied from the reference host's _queryCommands rather than guessed: five CAT
// verbs invented from their neighbours were wrong last time (WIP §6).
const std::set<std::string>& QueryCommands() {
  static const std::set<std::string> kQueries = {
      "FA;",  "FB;",  "IF;",  "MD0;", "TX;",  "PC;",  "ST;",  "FT;",  "VS;",
      "AG0;", "AG1;", "RG0;", "SM0;", "SM1;", "RM0;", "RM1;", "RM2;", "RM3;",
      "RM4;", "RM5;", "RM6;", "RM7;", "RM8;", "RM9;", "PA0;", "RA0;", "GT0;",
      "NB0;", "NR0;", "BC0;", "RT;",  "XT;",  "RD;",  "VX;",  "PR;",  "PR0;",
      "PR1;", "LK;",  "AN0;", "KS;",  "BI;",  "ID;",
  };
  return kQueries;
}

bool IsQuery(const std::string& cmd) { return QueryCommands().count(cmd) > 0; }

}  // namespace

TcpCatProxy::TcpCatProxy(RadioPoller* rig, int port) : rig_(rig), port_(port) {}

TcpCatProxy::~TcpCatProxy() { Stop(); }

bool TcpCatProxy::Start(std::string& err) {
  if (port_ <= 0) {
    err = "disabled";
    return false;
  }
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    err = std::string("socket: ") + std::strerror(errno);
    return false;
  }
  int yes = 1;
  ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  // ⚠️ LOOPBACK, NEVER INADDR_ANY. This forwards TX1; verbatim; binding it to
  // every interface would let anything on the network key the transmitter.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port_));

  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    err = std::string("bind 127.0.0.1:") + std::to_string(port_) + ": " + std::strerror(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::listen(listen_fd_, 4) < 0) {
    err = std::string("listen: ") + std::strerror(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  running_.store(true);
  stop_.store(false);
  accept_thread_ = std::thread([this] { AcceptLoop(); });
  return true;
}

void TcpCatProxy::Stop() {
  if (!running_.exchange(false) && listen_fd_ < 0) return;
  stop_.store(true);
  // Closing the listener is what wakes accept(); there is no portable way to
  // cancel a blocking accept otherwise.
  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }
  if (accept_thread_.joinable()) accept_thread_.join();
  for (auto& t : client_threads_) {
    if (t.joinable()) t.join();
  }
  client_threads_.clear();
}

void TcpCatProxy::AcceptLoop() {
  hdlog::Line(hdlog::kInfo, "CATPROXY",
              "listening on 127.0.0.1:" + std::to_string(port_) +
                  " (N1MM: Configure Ports -> TCP -> 127.0.0.1:" +
                  std::to_string(port_) + ")");
  while (!stop_.load()) {
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
      if (stop_.load()) break;
      continue;
    }
    // ⚠️ TCP_NODELAY. Nagle batches the small writes a CAT reply is made of, and
    // a logger asking for the frequency several times a second feels every
    // 40 ms of it.
    int yes = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    client_threads_.emplace_back([this, fd] { HandleClient(fd); });
  }
}

std::string TcpCatProxy::Forward(const std::string& cmd) {
  if (!rig_) return "";
  const bool query = IsQuery(cmd);

  auto result = std::make_shared<std::promise<std::string>>();
  auto fut = result->get_future();
  rig_->EnqueueTask([result, cmd, query](CatTransport& cat) {
    if (query) {
      const auto r = cat.Exchange(cmd);
      result->set_value(r.value_or(""));
    } else {
      cat.Send(cmd);
      result->set_value("");
    }
  });

  // ⚠️ Bounded wait. The queue is shared with the poller, so a stuck task must
  // not hold a client socket open forever - the logger would sit there looking
  // connected and getting nothing.
  if (fut.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
    hdlog::Line(hdlog::kInfo, "CATPROXY", "timed out forwarding " + cmd);
    return "";
  }
  commands_.fetch_add(1);
  return fut.get();
}

void TcpCatProxy::HandleClient(int fd) {
  clients_.fetch_add(1);
  hdlog::Line(hdlog::kInfo, "CATPROXY", "client connected");

  std::string pending;
  std::array<char, 1024> buf{};

  while (!stop_.load()) {
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) break;   // 0 = closed, <0 = error
    pending.append(buf.data(), static_cast<size_t>(n));

    // ⚠️ ONLY COMPLETE COMMANDS. A CAT command ends in ';'; a TCP read can split
    // one across two packets. Forwarding a fragment sends the radio half a
    // command and leaves the other half to be read as the start of the next -
    // every command after that point is shifted, which is not a crash, it is
    // wrong answers.
    size_t start = 0;
    while (true) {
      const size_t semi = pending.find(';', start);
      if (semi == std::string::npos) break;
      std::string cmd = pending.substr(start, semi - start + 1);
      start = semi + 1;

      // Strip anything a logger padded the line with; the command itself is
      // opcode plus payload plus ';'.
      while (!cmd.empty() && (cmd.front() == '\r' || cmd.front() == '\n' || cmd.front() == ' ')) {
        cmd.erase(cmd.begin());
      }
      if (cmd.size() < 2) continue;

      if (hdlog::On(hdlog::kVerbose)) {
        hdlog::Line(hdlog::kVerbose, "CATPROXY", ">>> " + cmd);
      }
      const std::string reply = Forward(cmd);
      if (!reply.empty()) {
        if (hdlog::On(hdlog::kVerbose)) {
          hdlog::Line(hdlog::kVerbose, "CATPROXY", "<<< " + reply);
        }
        if (::send(fd, reply.data(), reply.size(), MSG_NOSIGNAL) < 0) break;
      }
    }
    pending.erase(0, start);

    // A client that never sends a ';' must not grow this without bound.
    if (pending.size() > 4096) pending.clear();
  }

  ::close(fd);
  clients_.fetch_sub(1);
  hdlog::Line(hdlog::kInfo, "CATPROXY", "client disconnected");
}
