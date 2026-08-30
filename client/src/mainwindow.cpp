#include "mainwindow.h"

#include <QApplication>
#include "theme.h"
#include <QCloseEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QPair>
#include <QScreen>
#include <QScrollArea>
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
  // ⚠️ The panel goes inside a SCROLL AREA so the window can always be made
  // smaller than its content. Clamping the geometry is not enough on its own:
  // Qt honours the layout's minimum size hint, so a panel taller than the work
  // area forces the window bigger regardless of what setGeometry asked for -
  // and then the title bar is off-screen and the app cannot be reached at all.
  //
  // The selftest caught this the moment the DSP row and keypad were added, which
  // is exactly what it is for.
  auto* scroll = new QScrollArea;
  scroll->setWidget(BuildPanel());
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setCentralWidget(scroll);
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

  connect(&tx_audio_, &TxAudio::ArmedChanged, this,
          [this](bool armed, QString detail) {
            arm_button_->setChecked(armed);
            arm_button_->setText(armed ? "ARMED" : "ARM TX");
            // ⚠️ A test tone must be unmistakable in the UI. The whole risk of
            // having one is somebody transmitting it thinking it is a
            // microphone, so it shouts, in the transmit colour.
            const bool tone = tx_audio_.using_test_tone();
            arm_button_->setStyleSheet(
                armed ? QString("background:%1; border:1px solid %1; color:%2;"
                                "font-weight:bold;")
                            .arg(tone ? theme::kTx : theme::kAccent,
                                 tone ? "white" : "#08121f")
                      : QString());
            tx_label_->setText("tx: " + detail);
          });
  connect(&tx_audio_, &TxAudio::Sent, this, [this](qint64, int bps) {
    tx_label_->setText(QString("tx: %1 KiB/s out%2")
                           .arg(bps / 1024.0, 0, 'f', 1)
                           .arg(tx_audio_.using_test_tone() ? " — TEST TONE" : ""));
  });

  RestoreGeometryClamped();
}

QWidget* MainWindow::BuildPanel() {
  auto* root = new QWidget;
  auto* outer = new QVBoxLayout(root);

  // ── Readout ────────────────────────────────────────────────────────────────
  freq_label_ = new QLabel("—.———.———");
  QFont f("monospace", 46, QFont::Bold);
  f.setStyleHint(QFont::TypeWriter);
  freq_label_->setFont(f);
  freq_label_->setAlignment(Qt::AlignCenter);
  freq_label_->setStyleSheet(
      QString("color:%1; background:#0d0f12; border:1px solid %2;"
              "border-radius:8px; padding:10px 6px; letter-spacing:2px;")
          .arg(theme::kReadout, theme::kEdge));

  mode_label_ = new QLabel("—");
  vfo_label_ = new QLabel("VFO —");
  power_label_ = new QLabel("— W");
  for (QLabel* l : {mode_label_, vfo_label_, power_label_}) {
    QFont lf("monospace", 15, QFont::Bold);
    l->setFont(lf);
    l->setAlignment(Qt::AlignCenter);
    l->setStyleSheet(QString("color:%1; padding:2px;").arg(theme::kText));
  }

  auto* readout = new QVBoxLayout;
  readout->addWidget(freq_label_);
  auto* subline = new QHBoxLayout;
  subline->addWidget(mode_label_);
  subline->addWidget(vfo_label_);
  subline->addWidget(power_label_);
  readout->addLayout(subline);

  smeter_ = new SMeter;
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

  // ── Receiver / DSP ─────────────────────────────────────────────────────────
  auto* dsp = new QGroupBox("Receiver");
  auto* dl = new QHBoxLayout(dsp);
  struct DspSpec { const char* label; const char* field; const char* toggle; };
  static const DspSpec kDsp[] = {
      {"NB",    "nb",     "/api/nb/toggle"},
      {"NR",    "nr",     "/api/nr/toggle"},
      {"NOTCH", "notch",  "/api/notch/toggle"},
      {"ATT",   "att",    "/api/att/toggle"},
      {"VOX",   "vox",    "/api/vox/toggle"},
      {"COMP",  "comp",   "/api/comp/toggle"},
      {"MON",   "mon",    "/api/mon/toggle"},
      {"RIT",   "rit",    "/api/rit/toggle"},
  };
  for (const auto& d : kDsp) {
    auto* b = Btn(d.label, 62);
    b->setCheckable(true);
    const QString path = d.toggle;
    connect(b, &QPushButton::clicked, this, [this, path, b] {
      // Do NOT let the button keep its own checked state: re-assert it from the
      // rig on the next poll. Until then it may briefly disagree, which is
      // better than latching to a state the radio never reached.
      api_.Get(path);
    });
    dsp_[d.field] = b;
    dl->addWidget(b);
  }
  agc_button_ = Btn("AGC —", 84);
  connect(agc_button_, &QPushButton::clicked, this,
          [this] { api_.Get("/api/agc/cycle"); });
  dl->addWidget(agc_button_);
  outer->addWidget(dsp);

  // ── Frequency entry ────────────────────────────────────────────────────────
  auto* keypad = new QGroupBox("Frequency entry");
  auto* kl = new QHBoxLayout(keypad);
  buffer_label_ = new QLabel("");
  QFont bf("monospace", 16, QFont::Bold);
  buffer_label_->setFont(bf);
  buffer_label_->setMinimumWidth(120);
  buffer_label_->setAlignment(Qt::AlignCenter);
  buffer_label_->setStyleSheet(
      QString("color:%1; background:#0d0f12; border:1px solid %2; border-radius:4px;"
              "padding:4px;").arg(theme::kReadout, theme::kEdge));
  kl->addWidget(buffer_label_);
  for (int d = 0; d <= 9; ++d) {
    auto* b = Btn(QString::number(d), 34);
    connect(b, &QPushButton::clicked, this,
            [this, d] { api_.Get(QString("/api/freq/digit/%1").arg(d)); });
    kl->addWidget(b);
  }
  for (const auto& pair : QList<QPair<QString, QString>>{
           {"⌫", "/api/freq/backspace"}, {"CLR", "/api/freq/clear"},
           {"GO", "/api/freq/send"}}) {
    auto* b = Btn(pair.first, 44);
    const QString path = pair.second;
    connect(b, &QPushButton::clicked, this, [this, path] { api_.Get(path); });
    kl->addWidget(b);
  }
  outer->addWidget(keypad);

  // ── Transmit ───────────────────────────────────────────────────────────────
  auto* tx = new QGroupBox("Transmit");
  auto* tl = new QHBoxLayout(tx);
  ptt_button_ = Btn("PTT", 190);
  ptt_button_->setMinimumHeight(64);
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

  // ⚠️ ARM IS SEPARATE FROM PTT, AND THAT IS THE POINT.
  // Arming opens the audio channel and takes the host's single-transmitter
  // claim; PTT keys the rig. Doing both on one press would put a WebSocket
  // connect at the start of an over - the worst place for a delay - and would
  // leave the claim in doubt while the operator is already talking.
  arm_button_ = Btn("ARM TX", 96);
  arm_button_->setMinimumHeight(64);
  arm_button_->setCheckable(true);
  connect(arm_button_, &QPushButton::clicked, this, [this] {
    if (tx_audio_.armed()) {
      tx_audio_.Disarm();
    } else {
      ArmTransmit();
    }
  });
  tl->addWidget(arm_button_);

  auto* vol_label = new QLabel("VOLUME");
  vol_label->setStyleSheet(
      QString("color:%1; font-size:10px; font-weight:bold; letter-spacing:1px;")
          .arg(theme::kTextDim));
  // Transmit meters. Units come from the HOST - the client does not do the
  // conversion, so it cannot disagree with another client about what a reading
  // means. Power is a PERCENTAGE on purpose: the only watt table available is
  // for a 100 W radio and this rig is 200 W, so watts would read half.
  auto make_meter = [&](const char* caption, QLabel** out) {
    auto* box = new QVBoxLayout;
    auto* cap = new QLabel(caption);
    cap->setStyleSheet(QString("color:%1; font-size:9px; font-weight:bold;"
                              "letter-spacing:1px;").arg(theme::kTextDim));
    cap->setAlignment(Qt::AlignCenter);
    auto* val = new QLabel("—");
    QFont vf("monospace", 13, QFont::Bold);
    val->setFont(vf);
    val->setAlignment(Qt::AlignCenter);
    val->setStyleSheet(QString("color:%1;").arg(theme::kText));
    val->setMinimumWidth(64);
    box->addWidget(cap);
    box->addWidget(val);
    *out = val;
    return box;
  };
  tl->addSpacing(14);
  tl->addLayout(make_meter("SWR", &swr_label_));
  tl->addLayout(make_meter("ALC", &alc_label_));
  tl->addLayout(make_meter("PWR", &pwr_label_));

  tl->addSpacing(12);
  tl->addWidget(vol_label);
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
  tx_label_ = new QLabel("tx: disarmed");
  statusBar()->addPermanentWidget(tx_label_);
  statusBar()->addPermanentWidget(audio_label_);
  statusBar()->addWidget(conn_label_);
  return root;
}

void MainWindow::ResizeToContentForCapture() {
  if (auto* scroll = qobject_cast<QScrollArea*>(centralWidget())) {
    if (QWidget* inner = scroll->widget()) {
      const QSize want = inner->sizeHint();
      resize(want.width() + 24,
             want.height() + statusBar()->height() + 24);
    }
  }
}

void MainWindow::ArmTransmit() {
  const QString token = api_.SessionToken();
  if (token.isEmpty() || settings_.host.isEmpty()) {
    tx_label_->setText("tx: not connected");
    arm_button_->setChecked(false);
    return;
  }
  tx_audio_.Arm(QString("ws://%1:%2/ws/tx?token=%3")
                    .arg(settings_.host)
                    .arg(settings_.port)
                    .arg(token),
                settings_.tx_device_name);
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
  // Fetch the meter scale once. A host that does not serve it leaves the meter
  // unlabelled, which is the correct outcome rather than a guessed scale.
  api_.Get("/api/meters/scale", [this](QJsonObject scale) {
    QVector<SMeter::Tick> ticks;
    for (const QJsonValue& v : scale.value("ticks").toArray()) {
      ticks.push_back({v.toObject().value("raw").toInt(),
                       v.toObject().value("label").toString()});
    }
    if (!ticks.isEmpty()) smeter_->SetScale(ticks);
  });

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

  // The host owns the frequency-entry buffer, so every client sees the same
  // digits - including ones typed on another client's keypad.
  if (buffer_label_) {
    const QString buf = s.value("freq_buffer").toString();
    buffer_label_->setText(buf.isEmpty() ? "—" : buf);
  }

  const bool tx = s.value("tx").toBool();
  if (tx != tx_) {
    tx_ = tx;
    // The receiver is muted from the RIG's tx state, so every PTT source - this
    // panel, another client, the mic button on the radio - behaves the same.
    rx_.SetMutedForTx(tx);
    // ⚠️ Both driven from the RIG's tx state, not from the button. If the rig
    // is keyed by anything at all - another client, the mic button, a stuck
    // command - this client transmits its audio and mutes its receiver to
    // match. A client that only followed its own button would sit silent while
    // the transmitter was open.
    tx_audio_.SetKeyed(tx);
  }
  ptt_button_->setChecked(tx);
  ptt_button_->setText(tx ? "ON AIR" : "PTT");
  smeter_->SetTransmitting(tx);
  ptt_button_->setStyleSheet(
      tx ? QString("background:%1; border:1px solid %1; color:white;"
                   "font-weight:bold; font-size:15px; letter-spacing:2px;")
               .arg(theme::kTx)
         : QString("font-size:14px; letter-spacing:1px;"));

  // Count down the HOST's watchdog rather than inventing our own timeout.
  const int left = s.value("tx_timeout_in").toInt();
  if (tx && left > 0) {
    statusBar()->showMessage(QString("transmitting — watchdog drops PTT in %1 s").arg(left));
  } else if (!stale_) {
    statusBar()->showMessage(QString("cache %1 ms").arg(s.value("cache_age_ms").toInt()));
  }
}

void MainWindow::ApplyStatusFull(const QJsonObject& f) {
  // Every toggle reflects the RIG. If another client - or the front panel -
  // changes something, this follows within a poll rather than disagreeing.
  for (auto it = dsp_.begin(); it != dsp_.end(); ++it) {
    if (f.contains(it.key())) it.value()->setChecked(f.value(it.key()).toBool());
  }
  if (f.contains("agc")) {
    agc_button_->setText("AGC " + f.value("agc").toString());
  }
}

void MainWindow::ApplyMeters(const QJsonObject& m) {
  smeter_->SetValue(m.value("s_meter").toInt());
  // Only label the meter when the HOST supplies the unit. Deriving it here would
  // mean this client carrying a calibration for a radio it may not be on.
  if (m.contains("s_unit")) smeter_->SetUnitLabel(m.value("s_unit").toString());

  if (m.contains("swr_ratio")) {
    const double swr = m.value("swr_ratio").toDouble();
    swr_label_->setText(QString::number(swr, 'f', 1));
    // ⚠️ Above about 2:1 an operator wants to stop and look at the antenna, so
    // the number says so rather than sitting there in the same colour as a
    // perfect match.
    swr_label_->setStyleSheet(
        QString("color:%1;").arg(swr >= 2.0 ? theme::kTx : theme::kText));
  }
  if (m.contains("alc_pct")) {
    alc_label_->setText(QString("%1%").arg(m.value("alc_pct").toInt()));
  }
  if (m.contains("power_pct")) {
    // Percent, and labelled as such. Never watts - see the host's note.
    pwr_label_->setText(QString("%1%").arg(m.value("power_pct").toInt()));
  }
}

void MainWindow::SetStale(bool stale, const QString& detail) {
  if (stale == stale_) return;
  stale_ = stale;
  // Say it plainly. The host went to the trouble of reporting cache_age_ms and
  // stale precisely because an old reading once looked live for hours.
  conn_label_->setText(stale ? "⚠ " + detail : "connected to " + settings_.BaseUrl());
  // Grey the readout when the host says its cache is stale, so an old frequency
  // never sits there looking live.
  freq_label_->setStyleSheet(
      QString("color:%1; background:#0d0f12; border:1px solid %2;"
              "border-radius:8px; padding:10px 6px; letter-spacing:2px;")
          .arg(stale ? theme::kTextDim : theme::kReadout, theme::kEdge));
}

void MainWindow::RestoreGeometryClamped() {
  const QRect work = screen() ? screen()->availableGeometry()
                              : QApplication::primaryScreen()->availableGeometry();
  // The window must be able to shrink to the work area. Without this, a minimum
  // size hint from the layout wins and setGeometry silently does nothing.
  setMinimumSize(360, 240);
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
