#include "single_instance.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>

namespace {

// ⚠️ PER USER, NOT PER MACHINE. A fixed name would mean the second person to log
// in on a shared computer cannot start HamDeck at all - their copy would find
// the first user's server and quietly exit, which looks like the app refusing to
// launch for no reason.
QString ServerName() {
  const QByteArray user =
      qgetenv("USER") + qgetenv("USERNAME") + qgetenv("LOGNAME");
  return "hamdeck-" +
         QString::fromLatin1(
             QCryptographicHash::hash(user, QCryptographicHash::Sha1).toHex().left(12));
}

}  // namespace

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {}
SingleInstance::~SingleInstance() = default;

bool SingleInstance::Claim() {
  const QString name = ServerName();

  // Is somebody already there? A short timeout: this is a local socket, and a
  // long wait here is a long wait before the operator's window appears.
  {
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(300)) {
      probe.write("show");
      probe.waitForBytesWritten(300);
      probe.disconnectFromServer();
      return false;
    }
  }

  // ⚠️ A CRASH LEAVES THE SOCKET FILE BEHIND on Unix, and the next launch would
  // then fail to listen and exit believing another copy is running - the app
  // would never start again until somebody deleted a file they do not know
  // about. Nothing answered the probe above, so anything still there is stale.
  QLocalServer::removeServer(name);

  server_ = std::make_unique<QLocalServer>();
  if (!server_->listen(name)) {
    // ⚠️ Could not listen and nobody answered: rather than refuse to start,
    // carry on WITHOUT the guard. A missing safety net is better than an app
    // that will not launch, and the tray hazard it covers needs the operator to
    // do something specific before it bites.
    server_.reset();
    return true;
  }

  connect(server_.get(), &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket* c = server_->nextPendingConnection()) {
      connect(c, &QLocalSocket::disconnected, c, &QLocalSocket::deleteLater);
      emit showRequested();
    }
  });
  return true;
}
