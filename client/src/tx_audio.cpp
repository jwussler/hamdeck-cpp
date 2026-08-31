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
    if (!test_tone_) {
      // ⚠️ SIGNAL ON SUCCESS TOO, or the panel keeps the line it rendered when
      // the SOCKET connected - which is the moment before the microphone is
      // opened, so it reads "armed, NO MICROPHONE:" with no reason after it.
      // A working microphone then looked broken for as long as the panel was up,
      // and the empty reason sent an evening of debugging after a failure that
      // had never happened. Every path out of here now tells the UI something.
      if (OpenMic(device_name_)) {
        emit ArmedChanged(true, StatusLine());
      } else {
        emit ArmedChanged(true, "armed, but no usable microphone: " + last_error_);
      }
    }
  }
}

bool TxAudio::OpenMic(const QString& device_name) {
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

  // ⚠️ NEGOTIATE, DO NOT REFUSE.
  //
  // This asked for exactly 48000/16-bit/mono and returned false when the device
  // said no. A stereo-only USB microphone - which is how many of them enumerate
  // in Windows shared mode - failed on the channel count alone, and the panel
  // then armed, reported "armed", and transmitted SILENCE. The rig keyed into
  // nothing. Zero frames ever reached the host.
  //
  // 48000/16/mono is the HOST's wire format, not a requirement anyone can put on
  // the operator's microphone. Take what the device offers and convert.
  QAudioFormat fmt;
  fmt.setSampleRate(sample_rate_);
  fmt.setChannelCount(1);
  fmt.setSampleFormat(QAudioFormat::Int16);

  if (!chosen.isFormatSupported(fmt)) {
    const QAudioFormat native = chosen.preferredFormat();
    QAudioFormat alt;
    alt.setSampleRate(native.sampleRate() > 0 ? native.sampleRate() : sample_rate_);
    alt.setChannelCount(native.channelCount() > 0 ? native.channelCount() : 2);
    alt.setSampleFormat(QAudioFormat::Int16);

    if (!chosen.isFormatSupported(alt)) {
      // Last resort: the device's preferred format exactly as it reports it,
      // including its sample format. Float is common on Windows.
      alt = native;
    }
    if (!chosen.isFormatSupported(alt)) {
      // ⚠️ Say what the device DOES support. "cannot capture 48000 Hz/16-bit/
      // mono" sent us looking at the wrong end of the chain for an evening,
      // because it named what we asked for and never what was on offer.
      last_error_ = QString("%1 supports %2-%3 Hz, %4-%5 ch; cannot capture")
                        .arg(chosen.description())
                        .arg(chosen.minimumSampleRate())
                        .arg(chosen.maximumSampleRate())
                        .arg(chosen.minimumChannelCount())
                        .arg(chosen.maximumChannelCount());
      return false;
    }
    fmt = alt;
  }

  src_format_ = fmt;
  conv_ = PcmConverter(fmt.sampleRate(), fmt.channelCount(), sample_rate_);

  mic_ = std::make_unique<QAudioSource>(chosen, fmt);
  mic_dev_ = mic_->start();
  if (!mic_dev_) {
    last_error_ = QString("could not start capture on %1").arg(chosen.description());
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
  // Drop the resampler phase between overs, never during one: an over always
  // starts from a known state, and nothing is carried across a gap in which the
  // device kept running.
  if (!keyed_) conv_.Reset();
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

  // The device's native format, converted to the host's wire format here rather
  // than refused at open time. Passthrough when they already agree.
  if (conv_.passthrough() && src_format_.sampleFormat() == QAudioFormat::Int16) {
    return SendChunk(pcm);
  }

  const QByteArray native = src_format_.sampleFormat() == QAudioFormat::Int16
                                ? pcm
                                : FloatToInt16(pcm);
  const auto out = conv_.Convert(reinterpret_cast<const int16_t*>(native.constData()),
                                 static_cast<std::size_t>(native.size() / 2));
  if (out.empty()) return;
  SendChunk(QByteArray(reinterpret_cast<const char*>(out.data()),
                       static_cast<qsizetype>(out.size() * 2)));
}

// ⚠️ Windows commonly offers Float32 capture and nothing else. Scaling by 32767
// and clipping - not 32768, and not wrapping: a wrapped sample is a crack on the
// air, which is the same rule SendChunk applies to gain.
QByteArray TxAudio::FloatToInt16(const QByteArray& in) const {
  const int n = static_cast<int>(in.size() / static_cast<qsizetype>(sizeof(float)));
  QByteArray out(n * 2, Qt::Uninitialized);
  const auto* f = reinterpret_cast<const float*>(in.constData());
  auto* s = reinterpret_cast<qint16*>(out.data());
  for (int i = 0; i < n; ++i) {
    const double v = static_cast<double>(f[i]) * 32767.0;
    s[i] = static_cast<qint16>(v > 32767.0 ? 32767 : (v < -32768.0 ? -32768 : std::lround(v)));
  }
  return out;
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
  if (!mic_dev_) return "tx: armed, NO MICROPHONE: " + last_error_;
  // ⚠️ Name the negotiated format. When the mic is not running at the wire
  // format, the operator should be able to see that from the panel rather than
  // from a host-side counter.
  const QString mic = conv_.passthrough()
                          ? QString("mic 48k mono")
                          : QString("mic %1k/%2ch→48k mono")
                                .arg(conv_.src_rate() / 1000.0, 0, 'g', 3)
                                .arg(conv_.src_channels());
  return (keyed_ ? QString("tx: transmitting · ") : QString("tx: armed · ")) + mic;
}
