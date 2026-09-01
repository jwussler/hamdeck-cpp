// CW keyer text handling and memory channels.
//
// Two failure modes, both silent, both on the air:
//   - a wrong playback channel sends nothing, or the wrong memory, while the route says ok
//   - unfiltered text lets a caller append CAT commands to a transmitting radio

#include "check.h"
#include <cstdio>
#include <string>

#include "../src/api.h"

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);

  // ── THE ONE THE REFERENCE HOST GETS WRONG ────────────────────────────────
  // Hamlib's newcat_send_morse, which is validated against real radios rather than a
  // manual, maps memory 1-5 to channels 6-A for the FTDX-101MP and names the model
  // explicitly. 1-5 is the FT-710 exception. The C# host sends 1-5 for everything.
  CHECK(CwPlaybackChannel(1) == '6');
  CHECK(CwPlaybackChannel(2) == '7');
  CHECK(CwPlaybackChannel(3) == '8');
  CHECK(CwPlaybackChannel(4) == '9');
  CHECK(CwPlaybackChannel(5) == 'A');
  std::printf("channels: memory 1-5 -> KY6..KYA, per hamlib for this radio\n");

  // ⚠️ Out of range must be REFUSED, not clamped. Clamping a bad slot to a valid one puts
  // a transmission on the air that nobody asked for.
  CHECK(CwPlaybackChannel(0) == '\0');
  CHECK(CwPlaybackChannel(6) == '\0');
  CHECK(CwPlaybackChannel(-1) == '\0');
  CHECK(CwPlaybackChannel(99) == '\0');
  std::printf("refusal:  an out-of-range slot returns 0 rather than a nearby channel\n");

  // ── THE INJECTION, WHICH IS THE DANGEROUS ONE ────────────────────────────
  // A semicolon terminates a CAT command. "CQ;TX1;" in a KM1 payload would key the rig.
  CHECK(SanitizeCwText("CQ;TX1;") == "CQTX1");
  CHECK(SanitizeCwText("TEST;EX0301;") == "TESTEX0301");
  CHECK(SanitizeCwText(";;;") == "");
  std::printf("inject:   semicolons cannot survive into a CAT payload\n");

  // ── The ordinary behaviour ───────────────────────────────────────────────
  CHECK(SanitizeCwText("cq cq de wa0o") == "CQ CQ DE WA0O");
  CHECK(SanitizeCwText("  padded  ") == "PADDED");
  CHECK(SanitizeCwText("") == "");
  // The prosign punctuation a real exchange needs must SURVIVE - an over-strict filter
  // silently mangles callsigns and serials, which is its own kind of wrong.
  CHECK(SanitizeCwText("W1AW/4 ?") == "W1AW/4 ?");
  CHECK(SanitizeCwText("5NN 001 = TU + K") == "5NN 001 = TU + K");
  std::printf("allowed:  / ? . , - = + survive; case is normalised\n");

  // ⚠️ 50 characters is the KM limit. Longer must TRUNCATE, never be sent whole - an
  // over-long CAT command is not politely ignored by the radio.
  const std::string long_text(120, 'A');
  CHECK(SanitizeCwText(long_text).size() == kCwMaxLength);
  std::printf("length:   %zu chars capped at %zu\n", long_text.size(), kCwMaxLength);

  // Nothing sendable at all must come back empty so the caller can refuse. Sending an
  // empty KM1; is a command with no payload, not a no-op.
  CHECK(SanitizeCwText("!@#$%^&*()") == "");
  std::printf("empty:    unsendable input -> empty, so the caller sends nothing\n");

  // ── Percent-decoding, which the sanitizer alone cannot save you from ─────
  // ⚠️ "CQ%20CQ" without decoding becomes "CQ20CQ": the % is dropped and 2 and 0 are both
  // legal CW characters, so the corruption passes every other check and is transmitted.
  CHECK(UrlDecode("CQ%20CQ") == "CQ CQ");
  CHECK(SanitizeCwText(UrlDecode("CQ%20CQ")) == "CQ CQ");
  CHECK(SanitizeCwText("CQ%20CQ") == "CQ20CQ");   // the bug, if decoding is skipped
  CHECK(UrlDecode("W1AW%2F4") == "W1AW/4");
  CHECK(UrlDecode("5NN+001") == "5NN 001");
  // A malformed escape is left alone rather than guessed at; the sanitizer drops it.
  CHECK(UrlDecode("100%") == "100%");
  CHECK(UrlDecode("%zz") == "%zz");
  std::printf("decode:   %%20 and + become spaces; a broken escape is left alone\n");

  std::printf("\ncw: all checks passed\n");
  return 0;
}
