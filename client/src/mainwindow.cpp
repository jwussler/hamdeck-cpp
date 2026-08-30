#include "mainwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScreen>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {

QPushButton* Btn(const QString& text, int min_width = 56) {
  auto* b = new QPushButton(text);
  b->setMinimumWidth(min_width);
  b->setMinimumHeight(34);
  return b;
}

QString FormatHz(qint64 hz) {
  // 14.250.000 -> "14.250.00" reads badly; operators read MHz with three
  // decimals and then the rest smaller. Keep it simple and unambiguous.
  const qint64 mhz = hz / 1000000;
  const qint64 khz = (hz % 1000000) / 1000;
  const qint64 rest = hz % 1000;
  return QString("%1.%2.%3")
      .arg(mhz)
      .arg(khz, 3, 10, QChar('0'))
      .arg(rest, 3, 10, QChar('0'));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  settings_.Load();
  setWindowTitle("HamDeck");
  setCentralWidget(BuildPanel());
  statusBar()->showMessage("not connected");

  connect(&api_, &ApiClient::StatusUpdated, this, &MainWindow::ApplyStatus);
  connect(&api_, &ApiClient::StatusFullUpdated, this, &MainWindow::ApplyStatusFull);
  connect(&api_, &ApiClient::MetersUpdated, this, &MainWindow::ApplyMeters);
  connect(&api_, &ApiClient::ConnectionProblem, this,
          [this](QString why) { SetStale(true, why); });
  connect(&rx_, &RxAudio::ConnectionChanged, this, [this](bool up, QString detail) {
    audio_label_->setText(up ? "audio: streaming" : "audio: " + detail);
  });
  connect(&rx_, &RxAudio::BytesFlowing, this, [this](qint64 total, int bps) {
    audio_label_->setText(QString("audio: %1 KiB/s in%2")
                              .arg(bps / 1024.0, 0, 'f', 1)
                              .arg(rx_.CanPlay() ? "" : " (no playable device)"));
    Q_UNUSED(total);
  });
  connect(&rx_, &RxAudio::FormatNegotiated, this,
          [this](int rate, int ch, int bits) {
            audio_label_->setText(
                QString("audio: %1 Hz/%2-bit/%3ch").arg(rate).arg(bits).arg(ch));
          });

  RestoreGeometryClamped();
}

QWidget* MainWindow::BuildPanel() {
  auto* root = new QWidget;
  auto* outer = new QVBoxLayout(root);

  // ── Readout ────────────────────────────────────────────────────────────────
  freq_label_ = new QLabel("—.———.———");
  QFont f = freq_label_->font();
  f.setPointSize(34);
  f.setBold(true);
  f.setFamily("monospace");
  freq_label_->setFont(f);
  freq_label_->setAlignment(Qt::AlignCenter);

  mode_label_ = new QLabel("—");
  vfo_label_ = new QLabel("VFO —");
  power_label_ = new QLabel("— W");
  for (QLabel* l : {mode_label_, vfo_label_, power_label_}) {
    QFont lf = l->font();
    lf.setPointSize(13);
    lf.setBold(true);
    l->setFont(lf);
    l->setAlignment(Qt::AlignCenter);
  }

  auto* readout = new QVBoxLayout;
  readout->addWidget(freq_label_);
  auto* subline = new QHBoxLayout;
  subline->addWidget(mode_label_);
  subline->addWidget(vfo_label_);
  subline->addWidget(power_label_);
  readout->addLayout(subline);

  smeter_ = new QProgressBar;
  smeter_->setRange(0, 255);
  smeter_->setTextVisible(false);
  smeter_->setFixedHeight(14);
  readout->addWidget(smeter_);
  outer->addLayout(readout);

  // ── Bands ──────────────────────────────────────────────────────────────────
  auto* bands = new QGroupBox("Band");
  auto* bl = new QGridLayout(bands);
  const QStringList band_list{"160", "80", "60", "40", "30",
                              "20",  "17", "15", "12", "10", "6"};
  int col = 0, row = 0;
  for (const QString& b : band_list) {
    auto* btn = Btn(b + "m");
    connect(btn, &QPushButton::clicked, this,
            [this, b] { api_.Get("/api/band/" + b); });
    bl->addWidget(btn, row, col);
    if (++col == 6) { col = 0; ++row; }
  }
  outer->addWidget(bands);

  // ── Mode ───────────────────────────────────────────────────────────────────
  auto* modes = new QGroupBox("Mode");
  auto* ml = new QHBoxLayout(modes);
  for (const QString& m : QStringList{"LSB", "USB", "CW", "AM", "FM", "DATA"}) {
    auto* btn = Btn(m);
    const QString path =
        (m == "DATA") ? "/api/mode/data" : "/api/mode/" + m.toLower();
    connect(btn, &QPushButton::clicked, this, [this, path] { api_.Get(path); });
    ml->addWidget(btn);
  }
  outer->addWidget(modes);

  // ── VFO / tuning ───────────────────────────────────────────────────────────
  auto* vfo = new QGroupBox("VFO");
  auto* vl = new QHBoxLayout(vfo);
  struct { const char* label; const char* path; } vfo_buttons[] = {
      {"A", "/api/vfo/a"},          {"B", "/api/vfo/b"},
      {"Swap", "/api/vfo/swap"},    {"Split", "/api/split/toggle"},
      {"-1 kHz", "/api/step/1000/down"}, {"+1 kHz", "/api/step/1000/up"},
  };
  for (const auto& b : vfo_buttons) {
    auto* btn = Btn(b.label);
    const QString path = b.path;
    connect(btn, &QPushButton::clicked, this, [this, path] { api_.Get(path); });
    vl->addWidget(btn);
  }
  outer->addWidget(vfo);

  // ── Transmit ───────────────────────────────────────────────────────────────
  auto* tx = new QGroupBox("Transmit");
  auto* tl = new QHBoxLayout(tx);
  ptt_button_ = Btn("PTT", 120);
  ptt_button_->setMinimumHeight(52);
  ptt_button_->setCheckable(true);
  // ⚠️ PTT ON only. /api/ptt/off is not implemented on the host yet: unkeying
  // must wait for the audio still queued in the rig's buffer or the tail of
  // every transmission is lost. The button reflects the RIG's tx state from
  // /api/status, never its own checked state - a button that looks keyed while
  // the rig is not, or the reverse, is worse than no indicator.
  connect(ptt_button_, &QPushButton::clicked, this, [this] {
    api_.Get(tx_ ? "/api/ptt/off" : "/api/ptt/on");
  });
  tl->addWidget(ptt_button_);

  tl->addWidget(new QLabel("Volume"));
  volume_ = new QSlider(Qt::Horizontal);
  volume_->setRange(0, 100);
  volume_->setValue(settings_.volume);
  connect(volume_, &QSlider::valueChanged, this, [this](int v) {
    settings_.volume = v;
    rx_.SetVolume(v);
  });
  tl->addWidget(volume_);
  outer->addWidget(tx);

  conn_label_ = new QLabel("not connected");
  audio_label_ = new QLabel("audio: idle");
  statusBar()->addPermanentWidget(audio_label_);
  statusBar()->addWidget(conn_label_);
  return root;
}

bool MainWindow::ConnectTo(const QString& host, int port, const QString& user,
                           const QString& password, QString* error) {
  settings_.host = host;
  settings_.port = port;
  settings_.username = user;
  api_.SetBaseUrl(settings_.BaseUrl());

  bool ok = false;
  QString err;
  QEventLoop loop;
  api_.Login(user, password, [&](bool success, QString message) {
    ok = success;
    err = message;
    loop.quit();
  });
  loop.exec();

  if (!ok) {
    if (error) *error = err;
    conn_label_->setText("login failed: " + err);
    return false;
  }
  settings_.Save();
  conn_label_->setText("connected to " + settings_.BaseUrl());
  api_.StartPolling();
  rx_.SetVolume(settings_.volume);
  // Token in the query string: QWebSocket does not carry the REST cookie jar.
  const QString token = api_.SessionToken();
  if (token.isEmpty()) {
    audio_label_->setText("audio: no session token - stream would be refused");
  } else {
    rx_.Start(QString("ws://%1:%2/ws?token=%3").arg(host).arg(port).arg(token),
              settings_.rx_device_name);
  }
  return true;
}

void MainWindow::ApplyStatus(const QJsonObject& s) {
  SetStale(s.value("stale").toBool(), "host reports stale cache");
  freq_label_->setText(FormatHz(s.value("freq").toVariant().toLongLong()));
  mode_label_->setText(s.value("mode").toString("—"));
  vfo_label_->setText("VFO " + s.value("vfo").toString("—"));
  power_label_->setText(QString("%1 W").arg(s.value("power").toInt()));

  const bool tx = s.value("tx").toBool();
  if (tx != tx_) {
    tx_ = tx;
    // The receiver is muted from the RIG's tx state, so every PTT source - this
    // panel, another client, the mic button on the radio - behaves the same.
    rx_.SetMutedForTx(tx);
  }
  ptt_button_->setChecked(tx);
  ptt_button_->setText(tx ? "ON AIR" : "PTT");
  ptt_button_->setStyleSheet(tx ? "background:#c62828;color:white;font-weight:bold;"
                                : "");

  // Count down the HOST's watchdog rather than inventing our own timeout.
  const int left = s.value("tx_timeout_in").toInt();
  if (tx && left > 0) {
    statusBar()->showMessage(QString("transmitting — watchdog drops PTT in %1 s").arg(left));
  } else if (!stale_) {
    statusBar()->showMessage(QString("cache %1 ms").arg(s.value("cache_age_ms").toInt()));
  }
}

void MainWindow::ApplyStatusFull(const QJsonObject&) {}

void MainWindow::ApplyMeters(const QJsonObject& m) {
  smeter_->setValue(m.value("s_meter").toInt());
}

void MainWindow::SetStale(bool stale, const QString& detail) {
  if (stale == stale_) return;
  stale_ = stale;
  // Say it plainly. The host went to the trouble of reporting cache_age_ms and
  // stale precisely because an old reading once looked live for hours.
  conn_label_->setText(stale ? "⚠ " + detail : "connected to " + settings_.BaseUrl());
  freq_label_->setStyleSheet(stale ? "color:#999;" : "");
}

void MainWindow::RestoreGeometryClamped() {
  const QRect work = screen() ? screen()->availableGeometry()
                              : QApplication::primaryScreen()->availableGeometry();
  QRect g = settings_.window_geometry;
  if (!g.isValid() || g.width() < 200 || g.height() < 200) {
    g = QRect(0, 0, qMin(760, work.width()), qMin(620, work.height()));
  }
  g.setWidth(qMin(g.width(), work.width()));
  g.setHeight(qMin(g.height(), work.height()));
  // Re-centre if the saved position would put any part of the title bar off the
  // work area - a monitor that is no longer there is the usual cause.
  if (!work.contains(g)) {
    g.moveCenter(work.center());
  }
  setGeometry(g);
}

void MainWindow::closeEvent(QCloseEvent* e) {
  settings_.window_geometry = geometry();
  settings_.Save();
  rx_.Stop();
  api_.StopPolling();
  api_.Logout();
  e->accept();
}
