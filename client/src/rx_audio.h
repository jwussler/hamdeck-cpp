#pragma once

// Receiver audio: /ws -> speakers.
//
// The host sends one JSON `config` frame, then binary PCM. We do not assume the
// format - we take it from that frame, because assuming it is how a sample-rate
// change turns into chipmunks nobody can explain.

#include <QAudioSink>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QWebSocket>
#include <memory>

class RxAudio : public QObject {
  Q_OBJECT

 public:
  explicit RxAudio(QObject* parent = nullptr);
  ~RxAudio() override;

  // `device_name` is a device DESCRIPTION, not an index. Empty means the system
  // default. CARRYOVER.md section 6: indices shift when USB devices come and go,
  // which is what produced a dead microphone; and index 0 is not "the default",
  // it is arbitrary and out of range when there are no devices at all.
  void Start(const QString& ws_url, const QString& device_name);
  void Stop();

  void SetVolume(int percent);

  // ⚠️ Muted while the rig is keyed. Hearing yourself at the round-trip delay is
  // delayed auditory feedback - it disrupts speech so reliably that speech labs
  // use it deliberately. The operator slurs, hears themselves doing it, and
  // reports the link as broken (CARRYOVER.md section 4c).
  //
  // Driven off the rig's own tx state so every PTT source behaves alike, and
  // DROPS what queued on unmute so they come back live rather than replaying.
  void SetMutedForTx(bool keyed);

  bool connected() const { return connected_; }
  qint64 BytesReceived() const { return bytes_received_; }
  int SampleRate() const { return sample_rate_; }
  bool CanPlay() const { return sink_ok_; }

 signals:
  void ConnectionChanged(bool up, QString detail);
  // Bytes actually arriving. Reported separately from "playing", because
  // "connected but silent" and "not connected" look identical to an operator
  // and have completely different fixes.
  void BytesFlowing(qint64 total, int rate_bps);
  void FormatNegotiated(int sample_rate, int channels, int bits);

 private:
  void OnTextFrame(const QString& text);
  void OnBinaryFrame(const QByteArray& data);
  void OpenSink();

  QWebSocket socket_;
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* sink_dev_ = nullptr;
  QString device_name_;
  bool connected_ = false;
  bool muted_for_tx_ = false;
  int sample_rate_ = 22050;
  int channels_ = 1;
  int bits_ = 16;
  int volume_percent_ = 80;
  qint64 bytes_received_ = 0;
  qint64 last_report_bytes_ = 0;
  qint64 last_report_ms_ = 0;
  bool sink_ok_ = false;
};
