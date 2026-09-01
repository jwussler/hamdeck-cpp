// CW keyer text and memory channels.
//
// ⚠️ ITS OWN TRANSLATION UNIT WITH NO DEPENDENCIES, so the test that guards it links this
// file alone. Same reason as transmit_routes.cpp: the failures here are a wrong constant
// and a missing filter, and both are only catchable by a test that is cheap to run.

#include <string>

#include "api.h"

namespace {
// Characters an FTDX-101 CW memory will actually send: letters, digits, and the
// punctuation that has a prosign - / portable, ? interrogative, . , - for callsigns and
// serials, = for BT, + for AR.
constexpr const char* kAllowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /?.,-=+";
}  // namespace

// ⚠️ A SEMICOLON TERMINATES A CAT COMMAND. Unfiltered text in a KM1 payload lets a caller
// append whatever they like - "CQ;TX1;" keys the transmitter, "EX...;" rewrites menu
// settings on the radio. This is filtered HERE rather than at the route, because the route
// is not the only caller and the next one will forget.
//
// Nothing survives -> empty, and callers must treat that as "send nothing" rather than
// sending an empty command.
std::string SanitizeCwText(const std::string& text) {
  std::string out;
  out.reserve(text.size() < kCwMaxLength ? text.size() : kCwMaxLength);
  for (char raw : text) {
    if (out.size() >= kCwMaxLength) break;
    const char c = (raw >= 'a' && raw <= 'z') ? static_cast<char>(raw - 'a' + 'A') : raw;
    if (std::string(kAllowed).find(c) != std::string::npos) out.push_back(c);
  }
  // trim
  const auto b = out.find_first_not_of(' ');
  if (b == std::string::npos) return "";
  const auto e = out.find_last_not_of(' ');
  return out.substr(b, e - b + 1);
}

// Percent-decoding, for text arriving in a URL path.
//
// ⚠️ WITHOUT THIS, "CQ%20CQ" IS SENT AS "CQ20CQ". The percent is dropped by the sanitizer
// and the two hex digits are both legal CW characters, so the corruption survives every
// check and goes out on the air as a real transmission. A malformed escape is left alone
// rather than guessed at - the sanitizer then drops whatever cannot be sent.
std::string UrlDecode(const std::string& in) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') { out.push_back(' '); continue; }
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

// ⚠️ MEMORY 1-5 PLAYS BACK ON CHANNELS 6-A, NOT 1-5. THIS IS THE WHOLE POINT OF THIS FILE.
//
// The C# reference host sends KY1;..KY5; for memories 1..5. For an FTDX-101MP that is
// wrong. Hamlib's newcat_send_morse - validated against real radios rather than a PDF -
// names the models explicitly:
//
//     // 5-chan playback 6-A: ... FT-991, FTDX-101MP/D, FTDX10
//     // 5-chan but 1-5 playback: FT-710
//     if (!newcat_is_rig(rig, RIG_MODEL_FT710)) chan += 5;
//
// So 1-5 is the FT-710 EXCEPTION, and this station's radio is in the other group. Porting
// the reference faithfully would have shipped a keyer that plays nothing, or worse plays
// the wrong thing, with every route answering 200.
//
// Returns 0 for an out-of-range slot; callers must refuse rather than transmit.
char CwPlaybackChannel(int memory) {
  if (memory < 1 || memory > 5) return '\0';
  // 1->'6' 2->'7' 3->'8' 4->'9' 5->'A'
  return memory <= 4 ? static_cast<char>('0' + memory + 5) : 'A';
}
