#pragma once

// REST client for the HamDeck host.
//
// The session is a cookie the host sets on login. It is held in memory only and
// never written to disk (see settings.h). Qt's cookie jar handles it, so
// requests carry it automatically.

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

class ApiClient : public QObject {
  Q_OBJECT

 public:
  explicit ApiClient(QObject* parent = nullptr);

  void SetBaseUrl(const QString& url) { base_url_ = url; }
  QString BaseUrl() const { return base_url_; }

  void Login(const QString& user, const QString& password,
             std::function<void(bool ok, QString error)> done);
  void Logout();

  // Fire-and-forget rig command. `done` gets the parsed reply, or a null object
  // on transport failure.
  void Get(const QString& path, std::function<void(QJsonObject)> done = nullptr);

  // POST a JSON body. Used for the per-user settings profile; the session cookie
  // rides along the same way it does on a GET.
  void Post(const QString& path, const QByteArray& body,
            std::function<void(QJsonObject)> done = nullptr);

  // Starts polling /api/status. The interval matches the host's own 200ms cache
  // refresh - polling faster only re-reads the same cached values.
  void StartPolling(int interval_ms = 250);

  // ⚠️ THE POLL GOES FASTER WHILE THE RIG IS KEYED, and only then. The meters
  // are on a 1 Hz divider because frequency and mode do not move quickly; drive
  // does. An operator setting mic gain against a meter that updates once a
  // second is tuning against an average, which is exactly the measurement that
  // cannot tell a peak from a mumble.
  void SetTxActive(bool on) { tx_active_ = on; }
  void StopPolling();

  bool authenticated() const { return authenticated_; }

  // ⚠️ The session token, pulled out of the cookie jar for the WebSocket.
  // QWebSocket does NOT share QNetworkAccessManager's cookies, and a browser
  // cannot set headers on a WebSocket handshake either - which is exactly why
  // the host accepts ?token= as well as the cookie. Without this the audio
  // stream is refused at the upgrade and the panel silently has no receiver.
  QString SessionToken() const;

 signals:
  void StatusUpdated(QJsonObject status);
  void StatusFullUpdated(QJsonObject full);
  void MetersUpdated(QJsonObject meters);
  void BackendUpdated(QJsonObject backend);

  // ⚠️ Emitted when the host says the cache is stale, so the panel can SAY the
  // reading is old rather than show a stale frequency as if it were live. The
  // host went to the trouble of telling us; hiding it would waste that.
  void ConnectionProblem(QString reason);

 private:
  void PollOnce();

  QNetworkAccessManager net_;
  QString base_url_;
  QTimer poll_timer_;
  bool tx_active_ = false;
  bool authenticated_ = false;
  int full_divider_ = 0;
};
