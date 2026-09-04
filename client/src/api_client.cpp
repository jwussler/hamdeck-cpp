#include "api_client.h"

#include <chrono>

#include <QJsonDocument>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>

ApiClient::ApiClient(QObject* parent) : QObject(parent) {
  net_.setCookieJar(new QNetworkCookieJar(&net_));
  connect(&poll_timer_, &QTimer::timeout, this, &ApiClient::PollOnce);
}

void ApiClient::Login(const QString& user, const QString& password,
                      std::function<void(bool, QString)> done) {
  QNetworkRequest req{QUrl(base_url_ + "/api/auth/login")};
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  const QJsonObject body{{"username", user}, {"password", password}};
  auto* reply = net_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

  connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
    reply->deleteLater();
    const int code =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 200) {
      authenticated_ = true;
      if (done) done(true, {});
      return;
    }
    authenticated_ = false;

    // ⚠️ A TRANSPORT FAILURE IS NOT A LOGIN FAILURE, AND SAYING SO SENDS THE
    // OPERATOR TO THE WRONG FIX. When the name does not resolve, the port is
    // shut or the packet never arrives, there is no HTTP status at all and the
    // attribute reads 0 - which this reported as "login failed (0)". It cost a
    // real session of re-typing a password at a host that was never reached.
    // Same rule as the audio status line: "not arriving" and "refused" are
    // different problems and must not share a message.
    if (code == 0) {
      const QString where = QUrl(base_url_).host() + ":" +
                            QString::number(QUrl(base_url_).port(80));
      if (done) done(false, "no answer from " + where + " - " + reply->errorString());
      return;
    }

    // Surface the host's own message. It distinguishes bad credentials (401)
    // from the lockout (429), and a client that flattens both to "login failed"
    // leaves the operator retrying into a five-minute lockout.
    QString msg = QString("login failed (%1)").arg(code);
    const auto doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isObject() && doc.object().contains("message")) {
      msg = doc.object()["message"].toString();
    }
    if (done) done(false, msg);
  });
}

QString ApiClient::SessionToken() const {
  for (const QNetworkCookie& c : net_.cookieJar()->cookiesForUrl(QUrl(base_url_))) {
    if (c.name() == "hamdeck_session") return QString::fromUtf8(c.value());
  }
  return {};
}

void ApiClient::Logout() {
  QNetworkRequest req{QUrl(base_url_ + "/api/auth/logout")};
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  auto* reply = net_.post(req, QByteArray("{}"));
  connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
  authenticated_ = false;
}

void ApiClient::Get(const QString& path, std::function<void(QJsonObject)> done) {
  auto* reply = net_.get(QNetworkRequest{QUrl(base_url_ + path)});
  connect(reply, &QNetworkReply::finished, this, [reply, done] {
    reply->deleteLater();
    if (!done) return;
    const auto doc = QJsonDocument::fromJson(reply->readAll());
    done(doc.isObject() ? doc.object() : QJsonObject{});
  });
}

void ApiClient::Post(const QString& path, const QByteArray& body,
                     std::function<void(QJsonObject)> done) {
  QNetworkRequest req{QUrl(base_url_ + path)};
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  auto* reply = net_.post(req, body);
  connect(reply, &QNetworkReply::finished, this, [reply, done] {
    reply->deleteLater();
    if (!done) return;
    const auto doc = QJsonDocument::fromJson(reply->readAll());
    done(doc.isObject() ? doc.object() : QJsonObject{});
  });
}

void ApiClient::StartPolling(int interval_ms) { poll_timer_.start(interval_ms); }
void ApiClient::StopPolling() { poll_timer_.stop(); }

void ApiClient::PollOnce() {
  // ⚠️ TIMED FROM THE REQUEST THIS ALREADY MAKES. A dedicated ping would measure
  // a different path than the one carrying the panel's data, and would be one
  // more thing to keep honest.
  const auto sent_at = std::chrono::steady_clock::now();
  Get("/api/status", [this, sent_at](QJsonObject s) {
    if (s.isEmpty()) {
      emit ConnectionProblem("no reply from host");
      return;
    }
    // ⚠️ The host tells us when its own cache is stale. Pass that through rather
    // than painting an old frequency as current - the whole reason the host
    // carries cache_age_ms is that a stale reading once looked live for hours.
    if (s.value("stale").toBool()) {
      emit ConnectionProblem(
          QString("rig data is stale (%1 ms old)").arg(s.value("cache_age_ms").toInt()));
    }
    const int rtt = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - sent_at).count());
    rtt_ms_ = rtt;
    rtt_window_.append(rtt);
    while (rtt_window_.size() > 8) rtt_window_.removeFirst();
    if (rtt_window_.size() >= 3) {
      // Mean absolute deviation, not a standard deviation: it is the swing an
      // operator actually hears as broken audio, and it does not need a square
      // root to be honest about a set of eight samples.
      double mean = 0;
      for (int v : std::as_const(rtt_window_)) mean += v;
      mean /= rtt_window_.size();
      double dev = 0;
      for (int v : std::as_const(rtt_window_)) dev += qAbs(v - mean);
      jitter_ms_ = static_cast<int>(qRound(dev / rtt_window_.size()));
    }
    emit StatusUpdated(s);
  });

  // The slow-moving set does not need the fast cadence; the host only re-reads
  // it about once a second anyway.
  const bool slow_tick = (++full_divider_ % 4 == 0);
  if (slow_tick) {
    Get("/api/status/full", [this](QJsonObject f) {
      if (!f.isEmpty()) emit StatusFullUpdated(f);
    });
  }
  // ⚠️ METERS EVERY TICK WHILE KEYED. ALC and power are the numbers an operator
  // sets drive against, and at 1 Hz they are an average of an over rather than a
  // reading of one. Off the air they go back on the divider: nothing is moving,
  // and this is a poll against a radio, not a free number.
  if (slow_tick || tx_active_) {
    Get("/api/meters", [this](QJsonObject m) {
      if (!m.isEmpty()) emit MetersUpdated(m);
    });
  }
  // ⚠️ AND THE DRIVE ARRIVING AT THE RADIO, which is a HOST-side measurement.
  // The client knows what it sent; only the host knows what came out the other
  // end of the socket, and that is the number that says whether the rig is being
  // driven - the whole chain, not this end's intention.
  // ⚠️ ALWAYS, not only while keyed. tx_peak was the only reason to fetch this
  // before; rx_peak is a RECEIVE reading, and a receive meter that only updates
  // while transmitting is worse than none. Fast while keyed, on the divider
  // otherwise.
  if (tx_active_ || slow_tick) {
    Get("/api/backend", [this](QJsonObject b) {
      if (!b.isEmpty()) emit BackendUpdated(b);
    });
  }
}
