#include "rx_audio.h"

#include <QAudioDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QMediaDevices>

RxAudio::RxAudio(QObject* parent) : QObject(parent) {
  connect(&socket_, &QWebSocket::connected, this, [this] {
    connected_ = true;
    emit ConnectionChanged(true, "connected");
  });
  connect(&socket_, &QWebSocket::disconnected, this, [this] {
    connected_ = false;
    emit ConnectionChanged(false, "disconnected");
  });
  connect(&socket_,
          QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
          [this](QAbstractSocket::SocketError) {
            emit ConnectionChanged(false, socket_.errorString());
          });
  connect(&socket_, &QWebSocket::textMessageReceived, this, &RxAudio::OnTextFrame);
  connect(&socket_, &QWebSocket::binaryMessageReceived, this, &RxAudio::OnBinaryFrame);
}

RxAudio::~RxAudio() { Stop(); }

void RxAudio::Start(const QString& ws_url, const QString& device_name) {
  device_name_ = device_name;
  socket_.open(QUrl(ws_url));
}

void RxAudio::Stop() {
  socket_.close();
  if (sink_) {
    sink_->stop();
    sink_.reset();
    sink_dev_ = nullptr;
  }
}

void RxAudio::SetVolume(int percent) {
  volume_percent_ = qBound(0, percent, 100);
  if (sink_) sink_->setVolume(volume_percent_ / 100.0);
}

void RxAudio::SetMutedForTx(bool keyed) {
  if (keyed == muted_for_tx_) return;
  muted_for_tx_ = keyed;
  if (!keyed && sink_dev_) {
    // Coming back from transmit: throw away whatever queued while muted so the
    // operator hears LIVE audio, not a replay of the seconds they were talking.
    if (sink_) {
      sink_->reset();
      sink_dev_ = sink_->start();
    }
  }
}

void RxAudio::OnTextFrame(const QString& text) {
  const auto doc = QJsonDocument::fromJson(text.toUtf8());
  if (!doc.isObject()) return;
  const auto o = doc.object();
  if (o.value("type").toString() != "config") return;

  sample_rate_ = o.value("sample_rate").toInt(22050);
  channels_ = o.value("channels").toInt(1);
  bits_ = o.value("bits_per_sample").toInt(16);
  emit FormatNegotiated(sample_rate_, channels_, bits_);
  OpenSink();
}

void RxAudio::OpenSink() {
  QAudioFormat fmt;
  fmt.setSampleRate(sample_rate_);
  fmt.setChannelCount(channels_);
  fmt.setSampleFormat(bits_ == 16 ? QAudioFormat::Int16 : QAudioFormat::Int32);

  // Resolve the device BY NAME. If the remembered device is gone - unplugged,
  // renamed, a different machine - fall back to the SYSTEM DEFAULT, never to
  // "the first one in the list", which is arbitrary and may be a monitor input.
  QAudioDevice chosen = QMediaDevices::defaultAudioOutput();
  if (!device_name_.isEmpty()) {
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) {
      if (d.description() == device_name_) {
        chosen = d;
        break;
      }
    }
  }
  if (!chosen.isFormatSupported(fmt)) {
    sink_ok_ = false;
    emit ConnectionChanged(false,
                           QString("device cannot play %1 Hz/%2-bit/%3ch")
                               .arg(sample_rate_).arg(bits_).arg(channels_));
    return;   // the stream keeps arriving; we just cannot play it
  }
  sink_ok_ = true;

  sink_ = std::make_unique<QAudioSink>(chosen, fmt);
  sink_->setVolume(volume_percent_ / 100.0);
  sink_dev_ = sink_->start();
}

void RxAudio::OnBinaryFrame(const QByteArray& data) {
  bytes_received_ += data.size();

  // Muted for transmit: count the bytes so the stream is still visibly alive,
  // but write nothing. Dropping here rather than at the socket keeps the
  // connection warm, so unmuting is instant.
  // Report throughput whether or not we can play it. A stream that is arriving
  // but not audible is a DEVICE problem; one that is not arriving is a LINK or
  // AUTH problem. Collapsing them into "no audio" sends people to the wrong fix.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (last_report_ms_ == 0) last_report_ms_ = now;
  if (now - last_report_ms_ >= 1000) {
    const double secs = (now - last_report_ms_) / 1000.0;
    emit BytesFlowing(bytes_received_,
                      static_cast<int>((bytes_received_ - last_report_bytes_) / secs));
    last_report_bytes_ = bytes_received_;
    last_report_ms_ = now;
  }

  if (muted_for_tx_ || !sink_dev_) return;

  // QByteArray from Qt's signal is already a copy we own, so it can be written
  // directly. The rule this replaces still holds for raw callback APIs: audio
  // callback buffers are REUSED by the audio system, so copy before queuing.
  sink_dev_->write(data);
}
