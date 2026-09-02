// HamDeck C++ host.
//
// Route surface is in api.cpp; this file owns process startup and the listener
// split. See WIP.md for the road map and CARRYOVER.md for the traps.

#include <cstdlib>
#include <filesystem>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "api.h"
#include "cat_proxy.h"
#include "audio.h"
#include "http.h"
#include "auth.h"
#include "alsa_audio.h"
#include "cat_sim.h"
#include "qso_record.h"
#include "recorder.h"
#include "session_stats.h"
#include "tgxl.h"
#include "config.h"
#include "serial_cat.h"
#include "radio.h"
#include "tx_audio.h"
#include "version.h"

namespace {

std::atomic<bool> g_stop{false};
std::mutex g_stop_mu;
std::condition_variable g_stop_cv;

// Signal handlers may call almost nothing. Setting an atomic and notifying is
// the safe minimum; the real work happens on the main thread.
void OnStopSignal(int) {
  g_stop.store(true);
  g_stop_cv.notify_all();
}

// TWO LISTENERS, AND THE SPLIT IS LOAD-BEARING. Measured against the running C#
// host on the reference host on 08/30/2026:
//
//   :5001 control   - 127.0.0.1 ONLY, no session required. A LAN caller is
//                     refused: "The control API only serves this machine."
//   :5002 dashboard - LAN. Only /api/health and /api/auth/status are anonymous;
//                     everything else is 401 without a session (verified).
//
// The control port trusting every caller is exactly why it must be bound to the
// loopback address: the trust boundary is then enforced by the kernel, not by a
// check somebody can forget to write. /api/tune/amp must refuse every remote
// caller (CARRYOVER.md section 2), and "is this caller local" is answered by
// WHICH SOCKET ACCEPTED IT - never by a header, which the caller controls.
constexpr const char* kControlAddr = "127.0.0.1";
// ⚠️ DEFAULTS ONLY. The real values come from the config - api_port and
// dashboard_port. These were used directly for a while, which meant those two
// config keys were read, written back on save, and then silently ignored. A
// setting that does nothing is worse than no setting: the operator changes it,
// sees no effect, and has no way to tell whether the file or the host is wrong.
constexpr int         kControlPort = 5001;
constexpr const char* kDashAddr    = "0.0.0.0";
constexpr int         kDashPort    = 5002;

}  // namespace

// Walks the whole startup path - poller, audio, auth, both listeners - proves the
// process actually serves a request, and exits. CARRYOVER.md section 8: the .NET
// client shipped a release that could not launch at all while every test passed,
// because CI built the artifact and never ran it.
//
// ⚠️ A HANG IS A FAILURE, not a pass. CI must run this under an external timeout;
// a selftest that blocks forever looks exactly like one that is still working.
int SelfTest(RadioPoller& poller, RxAudioStream& rx, HttpServer& control,
             int control_port);

int main(int argc, char** argv) {
  bool selftest = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--selftest") {
      selftest = true;
    } else {
      // Unknown flags abort rather than being ignored. Silently accepting a
      // misspelled flag is how a safety option gets quietly disabled.
      std::cerr << "unknown argument: " << arg
                << "\nusage: hamdeck-host [--selftest]\n"
                   "  HAMDECK_CAT_DEVICE=<path>|auto   talk to the radio "
                   "(default: simulated rig)\n"
                   "  HAMDECK_ADMIN_HASH=pbkdf2:...    admin credential\n";
      return 2;
    }
  }

  // ⚠️ SIGPIPE ignored so a dropped client cannot kill the host.
  //
  // SIGTERM and SIGINT are HANDLED, but the handler must actually act and then
  // let the process die. An earlier version set a flag nothing read, so the
  // process caught the signal and kept serving - unkillable by normal means, and
  // it broke every systemctl restart. The rule: act, or do not install a handler.
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGTERM, OnStopSignal);
  std::signal(SIGINT, OnStopSignal);

  // ── Config ────────────────────────────────────────────────────────────────
  // A missing file is fine: the defaults are usable and name no station. A
  // file that EXISTS but is malformed is fatal - starting on defaults would run
  // the station on settings the operator did not choose and believes they
  // changed, including the transmit watchdog.
  Config config;
  std::string config_path;
  for (const auto& path : Config::DefaultPaths()) {
    std::string err;
    if (Config::Load(path, config, err)) {
      config_path = path;
      std::cout << "config: " << path << '\n' << std::flush;
      break;
    }
    if (err != "not found") {
      std::cerr << "FATAL: config " << path << ": " << err << '\n';
      return 1;
    }
  }

  // ⚠️ THE DEFAULT IS THE SIMULATOR, DELIBERATELY.
  // Talking to the radio has to be asked for. A host that hunts for a serial port
  // on startup would grab the CAT link the moment it ran anywhere near the
  // station - including on a laptop, in CI, or on a box that was only meant to be
  // built on. Set HAMDECK_CAT_DEVICE to a device path, or "auto" to probe the
  // usual candidates with ID; and nothing else.
  //
  // The backend is named in the banner and in /api/health's describe path, so a
  // simulator reading is never mistaken for the radio.
  std::unique_ptr<CatTransport> cat;
  bool simulated = false;
  const char* env_dev = std::getenv("HAMDECK_CAT_DEVICE");
  const std::string dev_str = env_dev ? env_dev : config.radio_port;
  if (!dev_str.empty()) {
    const char* dev = dev_str.c_str();
    auto serial = std::make_unique<SerialCat>();
    const bool ok =
        (std::string(dev) == "auto")
            ? serial->OpenFirstResponding({"/dev/ttyUSB0", "/dev/ttyUSB1"})
            : serial->Open(dev, config.radio_baud);
    if (!ok) {
      // Fail loudly and exit. Falling back to the simulator here would be the
      // worst possible behaviour: the host would come up looking healthy and
      // report a rig that is not there.
      std::cerr << "FATAL: could not open CAT device '" << dev
                << "'. Not falling back to the simulator - a host that reports a "
                   "rig it cannot reach is worse than one that refuses to start.\n";
      return 1;
    }
    std::cout << "CAT: " << serial->Describe() << '\n' << std::flush;
    cat = std::move(serial);
  } else {
    cat = std::make_unique<SimulatedRig>();
    simulated = true;
  }
  SessionStats session_stats;
  RadioPoller poller(std::move(cat));
  poller.SetSessionStats(&session_stats);
  poller.SetPttTimeoutSeconds(config.ptt_timeout_seconds);
  poller.Start();

  AuthService auth(config.web_session_timeout);
  for (const auto& u : config.web_users) {
    auth.AddUser(u.username, u.password_hash, u.is_admin, u.can_transmit,
                 u.is_station);
  }
  // Env override, for a throwaway run without writing a config file. It does not
  // replace the configured users, it adds to them.
  if (const char* hash = std::getenv("HAMDECK_ADMIN_HASH")) {
    auth.AddUser("admin", hash, /*is_admin=*/true, /*can_transmit=*/true,
                 // ⚠️ NOT a station account. This is the break-glass override for a
                 // throwaway run; it must not carry the right to start an unattended
                 // carrier just because it happens to be admin.
                 /*is_station=*/false);
  }

  // Synthetic RX audio: the codec is passed through to the reference host, so
  // there is no real capture device here. 22050 Hz mono/16-bit matches the wire
  // format the client expects (CARRYOVER.md section 2).
  // RX source: the real codec when one is named, a tone otherwise. A failure to
  // open a NAMED device is fatal, for the same reason a missing CAT device is:
  // a host that silently substitutes a test tone for the receiver would have the
  // operator listening to a sine wave and reporting the band dead.
  std::unique_ptr<AudioSource> rx_source;
  if (!config.alsa_capture_device.empty()) {
    auto cap = std::make_unique<AlsaCapture>();
    if (!cap->Open(config.alsa_capture_device, config.record_sample_rate)) {
      std::cerr << "FATAL: capture device '" << config.alsa_capture_device
                << "': " << cap->error()
                << "\nNot falling back to a test tone - an operator listening to a "
                   "sine wave would report the band dead.\n";
      return 1;
    }
    std::cout << "RX: " << cap->Describe() << '\n' << std::flush;
    rx_source = std::move(cap);
  } else {
    rx_source = std::make_unique<ToneSource>(config.record_sample_rate, 700.0);
  }
  RxAudioStream rx_audio(std::move(rx_source));

  // ⚠️ The recorder is fed from the same audio the operator hears. Recording is
  // OFF unless record_path is set - it does not pick a directory and start
  // filling a disk on its own.
  Recorder recorder(config.record_path, config.record_sample_rate,
                    config.record_buffer_seconds, config.record_max_seconds,
                    config.record_warning_seconds);
  if (recorder.available()) {
    std::cout << "recording: " << recorder.directory() << " (replay buffer "
              << config.record_buffer_seconds << "s)\n" << std::flush;
    rx_audio.SetRecorder(&recorder);
  }


  rx_audio.Start();

  // TX audio. The null sink discards: the codec is on the reference host, so
  // there is nowhere to play to. /api/tx-audio/status reports available:false
  // because of it, rather than claiming a working path.
  // TX sink: the real codec when named, a discarding sink otherwise. Same rule -
  // a named device that will not open is fatal rather than silently discarding,
  // because "transmitting" into a null sink puts a carrier on the air with
  // nothing modulating it.
  std::unique_ptr<TxAudioSink> tx_sink;
  AlsaPlayback* playback = nullptr;   // kept for the PTT tail measurement
  if (!config.alsa_playback_device.empty()) {
    auto pb = std::make_unique<AlsaPlayback>();
    if (!pb->Open(config.alsa_playback_device, 48000)) {
      std::cerr << "FATAL: playback device '" << config.alsa_playback_device
                << "' would not open. Not falling back to a discarding sink - "
                   "that would key the transmitter with nothing modulating it.\n";
      return 1;
    }
    std::cout << "TX: " << pb->Describe() << '\n' << std::flush;
    playback = pb.get();
    tx_sink = std::move(pb);
  } else {
    tx_sink = std::make_unique<NullTxSink>(48000);
  }
  TxAudioReceiver tx_audio(std::move(tx_sink));
  tx_audio.Start();

  // The external tuner. Not configured by default - it is a network device at an
  // address only the operator knows, and this repo ships no addresses.
  // ⚠️ THE TUNER DRIVES THE RADIO, NOT JUST THE TUNER BOX. It saves the power
  // and mode, sets 15 W CW, KEYS THE TRANSMITTER, tunes, then unkeys and puts
  // both back. Without the carrier the tuner has nothing to measure and the
  // button does nothing - which is exactly what the first port shipped.
  //
  // The setters QUEUE their CAT commands; they never touch the serial port, so
  // they are safe to call from the tuner's own thread. The getters read the
  // poller's cache, which is at most one 200 ms cycle old.
  RigControl tgxl_rig{
      .get_power = [&poller] { return poller.Snapshot().power; },
      .get_mode = [&poller] { return poller.Snapshot().mode; },
      .set_power = [&poller](int w) { poller.Enqueue(std::format("PC{:03d};", w)); },
      .set_mode = [&poller](const std::string& m) {
        poller.Enqueue(std::format("MD0{};", ModeCode(m)));
      },
      .set_ptt = [&poller](bool on) { poller.Enqueue(on ? "TX1;" : "TX0;"); },
  };
  TgxlTuner tgxl(config.tgxl_host, config.tgxl_port, tgxl_rig);

  // ⚠️ The amp tune is a TIMED CARRIER - 20 W CW for ten seconds, then 100 W.
  // No external device: the amplifier tunes itself against the RF. Local
  // callers only, enforced by the route on the loopback listener.
  AmpTuner amp(tgxl_rig);
  if (tgxl.configured()) std::cout << "TGXL: " << tgxl.Describe() << '\n' << std::flush;

  // ⚠️ FED FROM THE POLL LOOP, NOT ITS OWN TIMER. QsoRecorder is what turns PTT
  // into a recording, and it also hands the recorder the frequency and mode
  // that go in every sidecar - so it is wired up even when auto-record is off.
  // The tuner check is here rather than inside it because the amp and the TGXL
  // are what know a tune is running, and keying for a tune is PTT to the rig.
  QsoRecorder qso_record(&recorder, QsoRecorder::Options{
      config.ptt_record_enabled, config.ptt_record_seconds,
      static_cast<long long>(config.ptt_record_qsy_khz) * 1000});
  poller.OnPoll([&](bool connected, long long freq, const std::string& mode, bool tx) {
    const bool tuning = amp.IsActive() || tgxl.IsActive();
    qso_record.Observe(connected, freq, mode, tx, tuning);
  });
  if (config.ptt_record_enabled && recorder.available()) {
    std::cout << "ptt auto-record: on (" << config.ptt_record_seconds
              << "s idle, " << config.ptt_record_qsy_khz << " kHz QSY)\n" << std::flush;
  }

  HostState host_state;

  ApiDeps deps;
  deps.poller = &poller;
  deps.auth = &auth;
  deps.rx_audio = &rx_audio;
  deps.tx_audio = &tx_audio;
  deps.host = &host_state;
  deps.amp = &amp;
  deps.config = &config;
  deps.tgxl = &tgxl;
  deps.recorder = &recorder;
  deps.stats = &session_stats;
  // Where the config actually came from, so admin changes go back to the same
  // file rather than to a path that merely happens to be first in the search.
  deps.save_config = [&config, &config_path](std::string& err) {
    if (config_path.empty()) {
      err = "no config file was loaded - nothing to save to";
      return false;
    }
    return config.Save(config_path, err);
  };
  // Only a real playback device can answer this. With no device the callback
  // reports -1 and /api/ptt/off unkeys immediately, which is correct: there is
  // no audio queued to wait for.
  if (playback) {
    deps.queued_audio_ms = [playback] { return playback->QueuedMs(); };
  }
  // Per-user settings live beside the config, so a host with a config file gets
  // profiles and one running on defaults quietly does not - rather than picking
  // a directory nobody chose and writing the operator's settings into it.
  if (!config_path.empty()) {
    deps.profile_dir =
        std::filesystem::path(config_path).parent_path().string() + "/profiles";
  }
  // ── TCP CAT proxy ─────────────────────────────────────────────────────────
  // ⚠️ A proxy that will not start must NOT stop the host running the radio.
  // It is a convenience for a logger; the station is the job.
  TcpCatProxy cat_proxy(&poller, config.cat_proxy_port);
  if (config.cat_proxy_port > 0) {
    std::string perr;
    if (cat_proxy.Start(perr)) {
      std::cout << "cat proxy: 127.0.0.1:" << config.cat_proxy_port
                << " (N1MM: Configure Ports -> TCP)\n";
    } else {
      std::cerr << "cat proxy: NOT started - " << perr << '\n';
    }
  } else {
    std::cout << "cat proxy: off (set cat_proxy_port to enable)\n";
  }

  deps.simulated = simulated;
  deps.allow_anonymous_status = config.allow_anonymous_status;

  HttpServer control;
  HttpServer dashboard;
  const int control_port = config.api_port > 0 ? config.api_port : kControlPort;
  const int dash_port = config.dashboard_port > 0 ? config.dashboard_port : kDashPort;

  InstallRoutes(control, Listener::kControl, control_port, deps);
  InstallRoutes(dashboard, Listener::kDashboard, dash_port, deps);

  std::cout << kServiceName << ' ' << kVersion << '\n'
            << "CAT backend: " << poller.Backend() << '\n'
            << "rx audio: " << rx_audio.Backend() << '\n'
            << "tx audio: " << tx_audio.Backend() << '\n'
            << "watchdog: " << (config.ptt_timeout_seconds > 0
                   ? std::to_string(config.ptt_timeout_seconds) + "s" : "DISABLED") << '\n'
            << "auth: " << (auth.IsConfigured() ? "configured"
                                                : "NO USERS - dashboard will 401")
            << '\n' << std::flush;

  // civetweb listens on its own threads, so both Listen() calls return at once.
  const std::string control_spec =
      std::string(kControlAddr) + ":" + std::to_string(control_port);
  const std::string dash_spec = std::string(kDashAddr) + ":" + std::to_string(dash_port);

  if (!control.Listen(control_spec)) {
    std::cerr << "failed to bind " << control_spec << '\n';
    return 1;
  }
  std::cout << "control   " << control_spec << " (local only, no session)\n" << std::flush;

  if (!dashboard.Listen(dash_spec)) {
    std::cerr << "failed to bind " << dash_spec << '\n';
    return 1;
  }
  std::cout << "dashboard " << dash_spec << " (session required)\n" << std::flush;

  if (selftest) return SelfTest(poller, rx_audio, control, control_port);

  // Wait for a stop signal, then shut down in an order that leaves the RADIO
  // safe rather than the process tidy.
  {
    std::unique_lock<std::mutex> lock(g_stop_mu);
    g_stop_cv.wait(lock, [] { return g_stop.load(); });
  }
  std::cout << "\nshutting down\n" << std::flush;

  // 1. Stop accepting requests, so nothing can key the rig while we are unkeying.
  control.Stop();
  dashboard.Stop();

  // 2. Stop the poller so exactly one thread owns the serial port.
  const bool was_keyed = poller.Snapshot().tx;
  poller.Stop();

  // 3. ⚠️ DROP PTT. This is the whole reason the handler exists. The watchdog
  //    lives in this process; if we exit while keyed, nothing is left to unkey
  //    the rig and the station sits on an open carrier.
  if (was_keyed) {
    const bool confirmed = poller.UnkeyAndConfirm();
    std::cout << (confirmed ? "unkeyed on shutdown (rig confirmed)"
                            : "WARNING: could not confirm unkey on shutdown")
              << '\n' << std::flush;
  } else {
    std::cout << "not transmitting at shutdown\n" << std::flush;
  }

  rx_audio.Stop();
  tx_audio.Stop();
  return 0;
}

namespace {

bool Probe(const std::string& host, int port, const std::string& path, std::string& body) {
  const std::string cmd = "curl -sS --max-time 5 http://" + host + ":" +
                          std::to_string(port) + path;
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return false;
  char buf[1024];
  body.clear();
  while (fgets(buf, sizeof(buf), f)) body += buf;
  return pclose(f) == 0 && !body.empty();
}

}  // namespace

int SelfTest(RadioPoller& poller, RxAudioStream& rx, HttpServer&, int control_port) {
  int failures = 0;
  auto check = [&](const char* what, bool ok) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n' << std::flush;
    if (!ok) ++failures;
  };

  std::this_thread::sleep_for(std::chrono::milliseconds(600));

  check("poller has polled", poller.CacheAgeMs() >= 0);
  check("poller cache is fresh", poller.CacheAgeMs() < RadioPoller::kStaleAfterMs);
  check("rig reports connected", poller.Snapshot().connected);
  check("audio config frame well formed",
        rx.ConfigJson().find("\"sample_rate\":22050") != std::string::npos);

  std::string body;
  const bool got = Probe("127.0.0.1", control_port, "/api/health", body);
  check("control port answers /api/health", got);
  check("health says ok", body.find("\"status\":\"ok\"") != std::string::npos);

  const bool got_status = Probe("127.0.0.1", control_port, "/api/status", body);
  check("control port answers /api/status", got_status);

  std::cout << (failures ? "SELFTEST FAILED" : "SELFTEST PASSED") << '\n' << std::flush;
  return failures ? 1 : 0;
}
