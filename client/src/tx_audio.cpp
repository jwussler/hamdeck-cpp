#include "tx_audio.h"

#include <QAudioDevice>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QTimer>
#include <cmath>
#include <QtGlobal>

namespace {
// 20 ms at 48 kHz, matching the chunk size the host's pump loop expects.
constexpr int kFramesPerChunk = 960;
}  // namespace

TxAudio::TxAudio(QObject* parent) : QObject(parent) {
  connect(&socket_, &QWebSocket::connected, this, [this] {
    armed_ = true;
    emit ArmedChanged(true, test_tone_ ? "armed (TEST TONE - not a microphone)"
                                       : "armed");
  });
  connect(&socket_, &QWebSocket::disconnected, this, [this] {
    armed_ = false;
    emit ArmedChanged(false, last_error_.isEmpty() ? "disarmed" : last_error_);
  });
  connect(&socket_,
          QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
          [this](QAbstractSocket::SocketError) {
            last_error_ = socket_.errorString();
            armed_ = false;
            emit ArmedChanged(false, last_error_);
          });
  connect(&socket_, &QWebSocket::textMessageReceived, this, &TxAudio::OnTextFrame);
}

TxAudio::~TxAudio() { Disarm(); }

void TxAudio::OnTextFrame(const QString& text) {
  const auto doc = QJsonDocument::fromJson(text.toUtf8());
  if (!doc.isObject()) return;
  const auto o = doc.object();

  // ⚠️ The host refuses here for two reasons that are NOT the same, and the
  // operator needs to know which: no transmit permission on this account, or
  // somebody else already holds the transmitter. Collapsing them into "TX
  // failed" sends people to the wrong fix.
  if (o.value("type").toString() == "error") {
    last_error_ = o.value("message").toString();
    armed_ = false;
    emit ArmedChanged(false, last_error_);
    socket_.close();
    return;
  }
  if (o.value("type").toString() == "config") {
    // Take the rate from the host rather than assuming it. A mismatch here is
    // not a subtle artefact - it is a chipmunk or a drawl going out on the air.
    sample_rate_ = o.value("sample_rate").toInt(48000);
    if (!test_tone_ && !OpenMic(device_name_)) {
      emit ArmedChanged(true, "armed, but no usable microphone: " + last_error_);
    }
  }
}

bool TxAudio::OpenMic(const QString& device_name) {
  QAudioFormat fmt;
  fmt.setSampleRate(sample_rate_);
  fmt.setChannelCount(1);
  fmt.setSampleFormat(QAudioFormat::Int16);

  // Resolve BY NAME, falling back to the SYSTEM DEFAULT - never to "the first in
  // the list", which is arbitrary and on many machines is a monitor loopback.
  // Transmitting the desktop's own audio output would be a memorable mistake.
  QAudioDevice chosen = QMediaDevices::defaultAudioInput();
  if (!device_name.isEmpty()) {
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) {
      if (d.description() == device_name) {
        chosen = d;
        break;
      }
    }
  }
  if (chosen.isNull()) {
    last_error_ = "no audio input device";
    return false;
  }
  if (!chosen.isFormatSupported(fmt)) {
    last_error_ = QString("input cannot capture %1 Hz/16-bit/mono").arg(sample_rate_);
    return false;
  }

  mic_ = std::make_unique<QAudioSource>(chosen, fmt);
  mic_dev_ = mic_->start();
  if (!mic_dev_) {
    last_error_ = "could not start capture";
    return false;
  }
  connect(mic_dev_, &QIODevice::readyRead, this, &TxAudio::OnMicReady);
  return true;
}

void TxAudio::Arm(const QString& ws_url, const QString& device_name) {
  device_name_ = device_name;
  last_error_.clear();
  socket_.open(QUrl(ws_url));

  if (test_tone_) {
    // Paced by a timer rather than free-running: audio must be produced at real
    // time or the host's queue fills and every later frame arrives late.
    auto* t = new QTimer(this);
    t->setInterval(kFramesPerChunk * 1000 / sample_rate_);
    connect(t, &QTimer::timeout, this, &TxAudio::PushTestTone);
    connect(&socket_, &QWebSocket::disconnected, t, &QTimer::stop);
    t->start();
  }
}

void TxAudio::Disarm() {
  keyed_ = false;
  if (mic_) {
    mic_->stop();
    mic_.reset();
    mic_dev_ = nullptr;
  }
  if (socket_.state() != QAbstractSocket::UnconnectedState) socket_.close();
  armed_ = false;
}

void TxAudio::SetKeyed(bool keyed) {
  if (keyed == keyed_) return;
  keyed_ = keyed;
  // Nothing else to do: OnMicReady drains the capture buffer either way and only
  // SENDS while keyed. Stopping capture on unkey and restarting it on key would
  // put device start-up latency at the front of every over.
}

void TxAudio::OnMicReady() {
  if (!mic_dev_) return;
  const QByteArray pcm = mic_dev_->readAll();
  if (pcm.isEmpty()) return;

  // ⚠️ Read the buffer even when not keyed, and DISCARD it. Letting it back up
  // means the first thing transmitted on the next over is several seconds of the
  // room from before the operator pressed PTT.
  if (!keyed_ || !armed_) return;
  SendChunk(pcm);
}

void TxAudio::PushTestTone() {
  if (!keyed_ || !armed_) return;
  QByteArray pcm(kFramesPerChunk * 2, Qt::Uninitialized);
  auto* s = reinterpret_cast<qint16*>(pcm.data());
  const double step = 2.0 * M_PI * 700.0 / sample_rate_;
  for (int i = 0; i < kFramesPerChunk; ++i) {
    s[i] = static_cast<qint16>(6000.0 * std::sin(tone_phase_));
    tone_phase_ += step;
    if (tone_phase_ > 2.0 * M_PI) tone_phase_ -= 2.0 * M_PI;
  }
  SendChunk(pcm);
}

void TxAudio::SendChunk(const QByteArray& pcm) {
  // ⚠️ Gain is applied to a COPY and clipped at the 16-bit limits. Scaling in
  // place would corrupt Qt's buffer, and letting a scaled sample overflow wraps
  // it to the opposite polarity - which is a crack on the air, not distortion.
  QByteArray scaled;
  const QByteArray* out = &pcm;
  if (mic_gain_ != 100) {
    scaled = pcm;
    auto* s = reinterpret_cast<qint16*>(scaled.data());
    const int n = static_cast<int>(scaled.size() / 2);
    for (int i = 0; i < n; ++i) {
      const int v = static_cast<int>(s[i]) * mic_gain_ / 100;
      s[i] = static_cast<qint16>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
    out = &scaled;
  }
  return SendChunkRaw(*out);
}

void TxAudio::SendChunkRaw(const QByteArray& pcm) {
  // A 16-bit stream must go out in whole samples. An odd byte count shifts every
  // following sample by one byte, which is loud noise on the air rather than a
  // subtle glitch - the host refuses it, and there is no reason to make it.
  if (pcm.size() % 2 != 0) return;

  socket_.sendBinaryMessage(pcm);
  frames_sent_ += pcm.size() / 2;
  bytes_sent_ += pcm.size();

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (last_report_ms_ == 0) last_report_ms_ = now;
  if (now - last_report_ms_ >= 1000) {
    const double secs = (now - last_report_ms_) / 1000.0;
    emit Sent(bytes_sent_,
              static_cast<int>((bytes_sent_ - last_report_bytes_) / secs));
    last_report_bytes_ = bytes_sent_;
    last_report_ms_ = now;
  }
}

QString TxAudio::StatusLine() const {
  if (!armed_) return last_error_.isEmpty() ? "tx: disarmed" : "tx: " + last_error_;
  if (test_tone_) return "tx: ARMED — TEST TONE, NOT A MICROPHONE";
  if (!mic_dev_) return "tx: armed, no microphone";
  return keyed_ ? "tx: transmitting" : "tx: armed";
}
