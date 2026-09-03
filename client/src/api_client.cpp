#include "api_client.h"

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
  Get("/api/status", [this](QJsonObject s) {
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
  if (tx_active_) {
    Get("/api/backend", [this](QJsonObject b) {
      if (!b.isEmpty()) emit BackendUpdated(b);
    });
  }
}
