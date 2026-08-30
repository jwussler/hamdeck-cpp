#include "http.h"

#include <civetweb.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

const char* StatusText(int code) {
  switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 429: return "Too Many Requests";
    default:  return "Internal Server Error";
  }
}

HttpRequest BuildRequest(mg_connection* conn) {
  const mg_request_info* ri = mg_get_request_info(conn);
  HttpRequest req;
  req.path = ri->request_uri ? ri->request_uri : "";
  req.method = ri->request_method ? ri->request_method : "";
  req.query = ri->query_string ? ri->query_string : "";
  for (int i = 0; i < ri->num_headers; ++i) {
    req.headers[Lower(ri->http_headers[i].name)] = ri->http_headers[i].value;
  }
  // Body, for POST. Bounded: an unbounded read is a memory exhaustion primitive
  // for anyone who can reach the port.
  constexpr size_t kMaxBody = 64 * 1024;
  char buf[4096];
  int n;
  while ((n = mg_read(conn, buf, sizeof(buf))) > 0) {
    if (req.body.size() + n > kMaxBody) break;
    req.body.append(buf, n);
  }
  return req;
}

void WriteResponse(mg_connection* conn, const HttpResponse& res) {
  std::string head = "HTTP/1.1 " + std::to_string(res.status) + " " +
                     StatusText(res.status) + "\r\n";
  head += "Content-Type: " + res.content_type + "\r\n";
  head += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
  for (const auto& [k, v] : res.extra_headers) head += k + ": " + v + "\r\n";
  head += "Connection: close\r\n\r\n";
  mg_write(conn, head.data(), head.size());
  if (!res.body.empty()) mg_write(conn, res.body.data(), res.body.size());
}

}  // namespace

std::string HttpRequest::Header(const std::string& lowercase_key) const {
  const auto it = headers.find(lowercase_key);
  return it == headers.end() ? "" : it->second;
}

std::string HttpRequest::QueryParam(const std::string& key) const {
  char buf[512];
  const int n = mg_get_var(query.c_str(), query.size(), key.c_str(), buf, sizeof(buf));
  return n > 0 ? std::string(buf, n) : "";
}

bool WsConnection::SendBinary(const void* data, size_t len) {
  if (!open_) return false;
  mg_lock_connection(conn_);
  const int n = mg_websocket_write(conn_, MG_WEBSOCKET_OPCODE_BINARY,
                                   static_cast<const char*>(data), len);
  mg_unlock_connection(conn_);
  if (n <= 0) open_ = false;
  return n > 0;
}

bool WsConnection::SendText(const std::string& text) {
  if (!open_) return false;
  mg_lock_connection(conn_);
  const int n = mg_websocket_write(conn_, MG_WEBSOCKET_OPCODE_TEXT,
                                   text.data(), text.size());
  mg_unlock_connection(conn_);
  if (n <= 0) open_ = false;
  return n > 0;
}

struct HttpServer::Impl {
  mg_context* ctx = nullptr;
  std::map<std::string, HttpHandler> get_routes;
  std::map<std::string, HttpHandler> post_routes;
  std::map<std::string, PrefixHandler> prefix_routes;
  struct WsRoute { WsOpen on_open; WsClose on_close; WsData on_data; };
  std::map<std::string, WsRoute> ws_routes;
  std::mutex conn_mu;
  std::map<const mg_connection*, std::shared_ptr<WsConnection>> ws_conns;
  HttpServer* owner = nullptr;
};

HttpServer::HttpServer() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }
HttpServer::~HttpServer() { Stop(); }

void HttpServer::Get(const std::string& path, HttpHandler h) {
  impl_->get_routes[path] = std::move(h);
}
void HttpServer::Post(const std::string& path, HttpHandler h) {
  impl_->post_routes[path] = std::move(h);
}
void HttpServer::GetPrefix(const std::string& prefix, PrefixHandler h) {
  impl_->prefix_routes[prefix] = std::move(h);
}
void HttpServer::WebSocketRoute(const std::string& path, WsOpen on_open, WsClose on_close,
                               WsData on_data) {
  impl_->ws_routes[path] = {std::move(on_open), std::move(on_close), std::move(on_data)};
}

// civetweb dispatches by exact URI when a handler is registered per path, so one
// generic handler is registered and dispatch happens here. That keeps the auth
// gate in ONE place: it is impossible to add a route that bypasses it.
static int GenericHandler(mg_connection* conn, void* cbdata);

struct HandlerCtx {
  HttpServer::Impl* impl;
  PreRouting* pre;
  PreRouting* gate;
};

bool HttpServer::Listen(const std::string& listen_spec, int worker_threads) {
  static std::mutex ctx_mu;
  std::lock_guard<std::mutex> lock(ctx_mu);

  const std::string threads = std::to_string(worker_threads);
  const char* options[] = {
      "listening_ports", listen_spec.c_str(),
      "num_threads", threads.c_str(),
      "enable_directory_listing", "no",
      "enable_keep_alive", "no",
      nullptr};

  mg_callbacks callbacks{};
  impl_->ctx = mg_start(&callbacks, nullptr, options);
  if (!impl_->ctx) return false;

  auto* hc = new HandlerCtx{impl_.get(), &pre_, &gate_};
  mg_set_request_handler(impl_->ctx, "/", GenericHandler, hc);

  for (const auto& [path, route] : impl_->ws_routes) {
    mg_set_websocket_handler(
        impl_->ctx, path.c_str(),
        // ⚠️ CONNECT: the auth gate runs here too. RX audio is somebody's
        // operating activity and must not be reachable without a session - the
        // C# host made status AND audio require one from v3.4.14. Returning
        // non-zero rejects the upgrade before any frame is sent.
        [](const mg_connection* c, void* cb) -> int {
          auto* h = static_cast<HandlerCtx*>(cb);
          HttpRequest req = BuildRequest(const_cast<mg_connection*>(c));
          HttpResponse res;
          if (h->pre && *h->pre && !(*h->pre)(req, res)) return 1;
          return 0;
        },
        // READY: the upgrade succeeded. Register the client and hand it over.
        [](mg_connection* c, void* cb) {
          auto* h = static_cast<HandlerCtx*>(cb);
          const mg_request_info* ri = mg_get_request_info(c);
          const std::string path = ri->request_uri ? ri->request_uri : "";
          auto it = h->impl->ws_routes.find(path);
          if (it == h->impl->ws_routes.end()) return;
          auto conn = std::make_shared<WsConnection>(c);
          {
            std::lock_guard<std::mutex> lock(h->impl->conn_mu);
            h->impl->ws_conns[c] = conn;
          }
          if (it->second.on_open) it->second.on_open(BuildRequest(c), conn);
        },
        // DATA: inbound frames, on the connection's own thread. /ws is send-only
        // and simply keeps the connection open; /ws/tx consumes here.
        [](mg_connection* c, int bits, char* data, size_t len, void* cb) -> int {
          auto* h = static_cast<HandlerCtx*>(cb);
          const int opcode = bits & 0x0F;
          if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) return 0;

          const mg_request_info* ri = mg_get_request_info(c);
          const std::string path = ri && ri->request_uri ? ri->request_uri : "";
          auto it = h->impl->ws_routes.find(path);
          if (it == h->impl->ws_routes.end() || !it->second.on_data) return 1;

          std::shared_ptr<WsConnection> conn;
          {
            std::lock_guard<std::mutex> lock(h->impl->conn_mu);
            auto cit = h->impl->ws_conns.find(c);
            if (cit == h->impl->ws_conns.end()) return 1;
            conn = cit->second;
          }
          return it->second.on_data(conn, data, len,
                                    opcode == MG_WEBSOCKET_OPCODE_BINARY) ? 1 : 0;
        },
        // CLOSE
        [](const mg_connection* c, void* cb) {
          auto* h = static_cast<HandlerCtx*>(cb);
          std::shared_ptr<WsConnection> conn;
          {
            std::lock_guard<std::mutex> lock(h->impl->conn_mu);
            auto cit = h->impl->ws_conns.find(c);
            if (cit == h->impl->ws_conns.end()) return;
            conn = cit->second;
            h->impl->ws_conns.erase(cit);
          }
          conn->MarkClosed();
          const mg_request_info* ri = mg_get_request_info(c);
          const std::string path = ri && ri->request_uri ? ri->request_uri : "";
          auto it = h->impl->ws_routes.find(path);
          if (it != h->impl->ws_routes.end() && it->second.on_close) {
            it->second.on_close(conn);
          }
        },
        hc);
  }
  return true;
}

static int GenericHandler(mg_connection* conn, void* cbdata) {
  auto* hc = static_cast<HandlerCtx*>(cbdata);
  HttpRequest req = BuildRequest(conn);
  HttpResponse res;

  // ── The gate runs BEFORE dispatch, for every path, so a route added later
  // cannot skip it. Default is deny; anonymous routes are an explicit list.
  if (hc->pre && *hc->pre && !(*hc->pre)(req, res)) {
    WriteResponse(conn, res);
    return 200;
  }

  if (hc->gate && *hc->gate && !(*hc->gate)(req, res)) {
    WriteResponse(conn, res);
    return 200;
  }

  const auto& table = (req.method == "POST") ? hc->impl->post_routes : hc->impl->get_routes;
  const auto it = table.find(req.path);
  if (it == table.end()) {
    // Prefix routes, LONGEST MATCH FIRST. /api/freq/ must not shadow the more
    // specific /api/freq/set/, and map iteration order alone does not guarantee
    // that.
    const PrefixHandler* best = nullptr;
    size_t best_len = 0;
    std::string suffix;
    for (const auto& [prefix, handler] : hc->impl->prefix_routes) {
      if (req.path.rfind(prefix, 0) == 0 && prefix.size() > best_len) {
        best = &handler;
        best_len = prefix.size();
        suffix = req.path.substr(prefix.size());
      }
    }
    if (best) {
      (*best)(suffix, req, res);
      WriteResponse(conn, res);
      return 200;
    }
    // Shape matches the reference host: it names the path it could not route.
    res.status = 404;
    res.body = R"({"status":"error","message":"Unknown route","path":")" + req.path + R"("})";
    WriteResponse(conn, res);
    return 200;
  }
  it->second(req, res);
  WriteResponse(conn, res);
  return 200;
}

void HttpServer::Stop() {
  if (impl_ && impl_->ctx) {
    mg_stop(impl_->ctx);
    impl_->ctx = nullptr;
  }
}
