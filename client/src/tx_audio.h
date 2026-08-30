#pragma once

// Transmit audio: microphone -> /ws/tx -> the rig's USB codec.
//
// ⚠️ THIS PUTS A HUMAN VOICE ON THE AIR. Every design choice here is about not
// transmitting the wrong thing, and about not transmitting when nobody asked.
//
// 48000 Hz / 16-bit / mono, which is the host's wire format. RX is 22050 and TX
// is 48000 because the codec's capture does 8000-48000 but its PLAYBACK only
// does 32000-48000 - the asymmetry belongs to the device, not to a preference.

#include <QAudioSource>
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QWebSocket>
#include <memory>

class TxAudio : public QObject {
  Q_OBJECT

 public:
  explicit TxAudio(QObject* parent = nullptr);
  ~TxAudio() override;

  // ⚠️ ARMING AND KEYING ARE SEPARATE, DELIBERATELY.
  //
  // Arm() opens the socket and takes the host's single-transmitter claim. Frames
  // are only sent once the RIG reports it is keyed. Opening a WebSocket at the
  // instant the operator presses PTT would put a connect round trip at the worst
  // possible moment - the start of an over, which is exactly where clipping is
  // most noticeable - and it would leave the claim in doubt until it completed.
  //
  // `device_name` is a device DESCRIPTION, never an index: indices shift when
  // USB devices come and go, which is what produced a dead microphone before.
  void Arm(const QString& ws_url, const QString& device_name);
  void Disarm();
  bool armed() const { return armed_; }

  // Driven from the rig's own tx state, so every PTT source behaves alike -
  // this panel, another client, or the button on the microphone.
  void SetKeyed(bool keyed);

  // ⚠️ Mic gain, applied in software before the audio leaves this machine. The rig has
  // its own mic gain; this is the client's, so an operator can trim a quiet
  // headset without walking to the radio. Clipped, not wrapped - a sample that
  // overflows and wraps is a loud crack on the air rather than distortion.
  void SetMicGain(int percent) { mic_gain_ = qBound(0, percent, 200); }
  int mic_gain() const { return mic_gain_; }

  // A synthetic source, for proving the path on a machine with no microphone.
  // ⚠️ Named loudly and reported in every status line: a test tone that could be
  // mistaken for live audio would be worse than no test at all.
  void UseTestTone(bool on) { test_tone_ = on; }
  bool using_test_tone() const { return test_tone_; }

  qint64 FramesSent() const { return frames_sent_; }
  qint64 BytesSent() const { return bytes_sent_; }
  QString StatusLine() const;

 signals:
  void ArmedChanged(bool armed, QString detail);
  void Sent(qint64 bytes, int rate_bps);

 private:
  void OnMicReady();
  void OnTextFrame(const QString& text);
  void SendChunk(const QByteArray& pcm);
  void SendChunkRaw(const QByteArray& pcm);
  void PushTestTone();
  bool OpenMic(const QString& device_name);

  QWebSocket socket_;
  std::unique_ptr<QAudioSource> mic_;
  QIODevice* mic_dev_ = nullptr;
  QString device_name_;

  bool armed_ = false;
  bool keyed_ = false;
  bool test_tone_ = false;
  int  mic_gain_ = 100;   // percent; 100 = unity
  QString last_error_;

  int sample_rate_ = 48000;
  double tone_phase_ = 0.0;

  qint64 frames_sent_ = 0;
  qint64 bytes_sent_ = 0;
  qint64 last_report_ms_ = 0;
  qint64 last_report_bytes_ = 0;
};
