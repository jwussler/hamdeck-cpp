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
  if (++full_divider_ % 4 == 0) {
    Get("/api/status/full", [this](QJsonObject f) {
      if (!f.isEmpty()) emit StatusFullUpdated(f);
    });
    Get("/api/meters", [this](QJsonObject m) {
      if (!m.isEmpty()) emit MetersUpdated(m);
    });
  }
}
