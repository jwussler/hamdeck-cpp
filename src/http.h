#pragma once

// Thin server wrapper over civetweb.
//
// Exists so the route table in api.cpp does not care what HTTP library is
// underneath - the last swap (cpp-httplib -> civetweb, forced by needing
// WebSocket and HTTP on one port) touched every route, and that should not
// happen twice.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct mg_context;
struct mg_connection;

struct HttpRequest {
  std::string path;
  std::string method;
  std::string body;
  std::map<std::string, std::string> headers;   // lowercased keys
  std::string query;
  std::string QueryParam(const std::string& key) const;
  std::string Header(const std::string& lowercase_key) const;
};

struct HttpResponse {
  int status = 200;
  std::string body;
  std::string content_type = "application/json";
  std::vector<std::pair<std::string, std::string>> extra_headers;
};

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;
// A prefix route receives the part of the path after the prefix.
using PrefixHandler =
    std::function<void(const std::string& suffix, const HttpRequest&, HttpResponse&)>;

// Return false to reject the request with the response as filled in. Runs before
// every handler, including WebSocket upgrades - an audio stream is somebody's
// operating activity and must not be reachable without a session.
using PreRouting = std::function<bool(const HttpRequest&, HttpResponse&)>;

// One live WebSocket client.
//
// ⚠️ Writes are serialised per connection. Two threads writing the same socket
// interleave frames and corrupt the stream - the C# client hit the same wall from
// the other side, where overlapping SendAsync calls are rejected outright
// (CARRYOVER.md section 6). Here the lock is taken inside Send*.
class WsConnection {
 public:
  explicit WsConnection(mg_connection* conn) : conn_(conn) {}

  bool SendBinary(const void* data, size_t len);
  bool SendText(const std::string& text);
  void MarkClosed() { open_ = false; }
  bool open() const { return open_; }

 private:
  mg_connection* conn_;
  bool open_ = true;
};

using WsOpen  = std::function<void(const HttpRequest&, std::shared_ptr<WsConnection>)>;
using WsClose = std::function<void(std::shared_ptr<WsConnection>)>;
// Inbound frames. Return false to close the connection.
using WsData  = std::function<bool(std::shared_ptr<WsConnection>, const char* data,
                                   size_t len, bool is_binary)>;

class HttpServer {
 public:
  HttpServer();
  ~HttpServer();

  void SetPreRouting(PreRouting pre) { pre_ = std::move(pre); }
  // A second gate, run after auth and before dispatch. Used for the software VFO
  // lock, which is not a permission level - it is the operator's instruction
  // about their own radio and applies to every caller.
  void SetSecondGate(PreRouting gate) { gate_ = std::move(gate); }
  // A third gate, for the admin routes. Separate from the others because it
  // answers a different question - "which user is this" rather than "is there a
  // session" or "has the operator locked the VFO".
  void SetAdminGate(PreRouting gate) { admin_gate_ = std::move(gate); }
  void Get(const std::string& path, HttpHandler h);
  void Post(const std::string& path, HttpHandler h);

  // Matched only when no exact route matches, longest prefix first - so
  // /api/freq/set/ cannot be shadowed by a shorter /api/freq/ registered later.
  void GetPrefix(const std::string& prefix, PrefixHandler h);
  void WebSocketRoute(const std::string& path, WsOpen on_open, WsClose on_close,
                      WsData on_data = nullptr);

  // listen_spec is a civetweb ports string, e.g. "127.0.0.1:5001". Binding an
  // explicit loopback address is what makes "local only" a kernel guarantee
  // rather than a check somebody can forget to write.
  bool Listen(const std::string& listen_spec, int worker_threads = 8);
  void Stop();

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
  PreRouting pre_;
  PreRouting gate_;
  PreRouting admin_gate_;
};
