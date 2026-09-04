#include "tray.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>

namespace {

// ⚠️ DRAWN, NOT THREE ARTWORK FILES. The mark is one shape; what changes is a
// dot in the corner, and drawing it here means the three states cannot drift
// apart the way three exported PNGs would. It has to read at 16 px, so it is a
// dot at the corner rather than a tint of the whole mark - a tinted 16 px mark
// is a smudge, and this is the difference between "on the air" and "not".
QIcon Compose(const QIcon& base, const QColor& dot) {
  if (!dot.isValid()) return base;
  QPixmap pm = base.pixmap(32, 32);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(0, 0, 0, 180), 2));
  p.setBrush(dot);
  p.drawEllipse(QPointF(pm.width() - 9, pm.height() - 9), 7, 7);
  p.end();
  return QIcon(pm);
}

}  // namespace

Tray::Tray(QObject* parent) : QObject(parent) {}
Tray::~Tray() = default;

bool Tray::Available() { return QSystemTrayIcon::isSystemTrayAvailable(); }

bool Tray::Install() {
  if (!Available()) return false;
  icon_ = std::make_unique<QSystemTrayIcon>();
  menu_ = new QMenu();

  QAction* show = menu_->addAction("Show HamDeck");
  connect(show, &QAction::triggered, this, [this] { emit showRequested(); });
  menu_->addSeparator();

  // ⚠️ QUIT IS AN ACTION, NOT A CLOSE. It releases the transmitter: disarm,
  // unkey, drop /ws/tx - and the host's close handler then puts the power cap
  // back and MOD SOURCE back to MIC. The window's X button deliberately does
  // NOT do this; that is the whole difference between hiding and quitting.
  QAction* quit = menu_->addAction("Quit HamDeck");
  connect(quit, &QAction::triggered, this, [this] { emit quitRequested(); });

  icon_->setContextMenu(menu_);
  connect(icon_.get(), &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
              emit showRequested();
            }
          });
  Repaint();
  icon_->show();
  return true;
}

void Tray::SetState(bool session, bool armed, bool tx, const QString& radio_note) {
  if (session == session_ && armed == armed_ && tx == tx_ && radio_note == radio_note_) return;
  session_ = session;
  armed_ = armed;
  tx_ = tx;
  radio_note_ = radio_note;
  Repaint();
}

void Tray::Repaint() {
  if (!icon_) return;
  const QIcon base = QIcon(":/icons/hamdeck-32.png");

  // ⚠️ THE WORDS MATTER MORE THAN THE COLOUR. An icon is a colour somebody has
  // to remember the meaning of; the tooltip says what is true, including that
  // the radio is on REAR - which is what makes the hand mic dead at the rig.
  QColor dot;
  QString text;
  if (tx_) {
    dot = QColor("#B4232A");
    text = "HamDeck — ON AIR";
  } else if (armed_) {
    dot = QColor("#FFB020");
    text = "HamDeck — ARMED, holding the transmitter";
  } else if (session_) {
    dot = QColor("#32C765");
    text = "HamDeck — connected";
  } else {
    text = "HamDeck — not connected";
  }
  if (!radio_note_.isEmpty()) text += "\n" + radio_note_;

  icon_->setIcon(Compose(base, dot));
  icon_->setToolTip(text);
}

void Tray::ShowFirstHideHint() {
  // ⚠️ ONCE, AND ONLY ON THE FIRST HIDE. An app that vanishes silently is one
  // the operator assumes has stopped - and this one may still be holding their
  // transmitter. Saying it every time would train them to dismiss it.
  if (hinted_ || !icon_) return;
  hinted_ = true;
  icon_->showMessage("HamDeck is still running",
                     armed_ || tx_
                         ? "It still holds the transmitter. Quit from here to hand the radio back."
                         : "Closing the window does not disconnect. Quit from here to stop it.",
                     QSystemTrayIcon::Information, 6000);
}
