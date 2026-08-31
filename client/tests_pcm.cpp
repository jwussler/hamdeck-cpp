// The capture-format conversion: whatever the microphone gives us -> 48000 Hz,
// 16-bit, mono, which is the host's wire format.
//
// ⚠️ WRITTEN AGAINST A REAL FAILURE. The client demanded exactly 48000/16/mono
// and refused anything else, so a stereo USB microphone armed the panel,
// reported "armed", and sent NOTHING - the host's /api/backend counters read
// tx_accepted:0, tx_dropped:0 after a live keyup. These cases cover the three
// properties that failure needed: a stereo device is downmixed rather than
// refused, a rate mismatch is resampled, and the resampler carries its phase
// across chunk boundaries instead of restarting every 20 ms.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "src/pcm_convert.h"

static int failures = 0;

static void Check(const char* what, bool ok, const std::string& detail = "") {
    std::printf("  %s %s%s\n", ok ? "ok  " : "FAIL", what,
                detail.empty() ? "" : ("  " + detail).c_str());
    if (!ok) ++failures;
}

static void EqInt(const char* what, long long got, long long want) {
    Check(what, got == want,
          got == want ? "" : ("got " + std::to_string(got) + " wanted " + std::to_string(want)));
}

int main() {
    std::printf("pcm conversion\n");

    // ── A device that already speaks the wire format is not touched ──────────
    {
        PcmConverter c(48000, 1, 48000);
        Check("48k mono is passthrough", c.passthrough());
        const std::vector<int16_t> in = {1, -2, 3, -4};
        const auto out = c.Convert(in.data(), in.size());
        Check("passthrough preserves samples", out == in);
    }

    // ── Stereo is DOWNMIXED, not refused ─────────────────────────────────────
    // This is the case that was silently fatal.
    {
        PcmConverter c(48000, 2, 48000);
        Check("48k stereo is not passthrough", !c.passthrough());
        // Interleaved L,R.
        const std::vector<int16_t> in = {100, 200, -100, -200, 1000, 0};
        const auto out = c.Convert(in.data(), in.size());
        EqInt("stereo halves the sample count", (long long)out.size(), 3);
        EqInt("frame 0 averaged", out[0], 150);
        EqInt("frame 1 averaged", out[1], -150);
        EqInt("frame 2 averaged", out[2], 500);
    }

    // ⚠️ A capsule wired to ONE channel of a stereo stream. Averaging halves it;
    // taking the wrong channel would transmit silence, which is the failure this
    // whole file exists for.
    {
        PcmConverter c(48000, 2, 48000);
        const std::vector<int16_t> in = {8000, 0, -8000, 0};
        const auto out = c.Convert(in.data(), in.size());
        EqInt("one-sided capsule survives downmix", out[0], 4000);
        EqInt("one-sided capsule survives downmix (2)", out[1], -4000);
    }

    // ── A rate mismatch is resampled ─────────────────────────────────────────
    {
        PcmConverter c(44100, 1, 48000);
        Check("44.1k mono is not passthrough", !c.passthrough());
        std::vector<int16_t> in(4410);
        for (size_t i = 0; i < in.size(); ++i) {
            in[i] = static_cast<int16_t>(8000.0 * std::sin(2.0 * M_PI * 700.0 * (double)i / 44100.0));
        }
        const auto out = c.Convert(in.data(), in.size());
        // 100 ms in should be ~100 ms out: 4800 samples, within a sample or two.
        Check("44.1k -> 48k gives ~4800 samples",
              out.size() >= 4795 && out.size() <= 4801,
              "got " + std::to_string(out.size()));
        // Not silence, and not clipped: a resampler that returns zeros would
        // pass a size check and transmit nothing.
        int16_t peak = 0;
        for (int16_t s : out) peak = std::max<int16_t>(peak, std::abs(s));
        Check("resampled audio is not silence", peak > 7000 && peak <= 8100,
              "peak " + std::to_string(peak));
    }

    // ── Phase is carried ACROSS chunks ───────────────────────────────────────
    // ⚠️ Tested by MEASURING THE SEAM, not by comparing sample counts. A
    // resampler that reset its phase every chunk would still produce about the
    // right number of samples - the count is the thing that looks fine while the
    // audio buzzes at the chunk rate. So: resample one second in 441-sample
    // chunks, then look for a step at the joins that the continuous version does
    // not have.
    {
        PcmConverter whole(44100, 1, 48000);
        PcmConverter chunked(44100, 1, 48000);
        std::vector<int16_t> in(44100);
        for (size_t i = 0; i < in.size(); ++i) {
            in[i] = static_cast<int16_t>(6000.0 * std::sin(2.0 * M_PI * 440.0 * (double)i / 44100.0));
        }
        const auto a = whole.Convert(in.data(), in.size());
        Check("one second resamples to ~48000 samples",
              a.size() >= 47998 && a.size() <= 48000, "got " + std::to_string(a.size()));

        std::vector<int16_t> joined;
        for (size_t off = 0; off + 441 <= in.size(); off += 441) {
            const auto part = chunked.Convert(in.data() + off, 441);
            joined.insert(joined.end(), part.begin(), part.end());
        }
        Check("chunked total is within a sample of whole",
              joined.size() + 1 >= a.size() && joined.size() <= a.size() + 1,
              "chunked " + std::to_string(joined.size()) + " whole " + std::to_string(a.size()));

        auto max_step = [](const std::vector<int16_t>& v) {
            int worst = 0;
            for (size_t i = 1; i < v.size(); ++i) {
                worst = std::max(worst, std::abs((int)v[i] - (int)v[i - 1]));
            }
            return worst;
        };
        const int step_whole = max_step(a);
        const int step_chunked = max_step(joined);
        // A 440 Hz sine at 48k steps by ~350 between samples. A phase reset at a
        // chunk join would put a step of thousands in there.
        Check("no discontinuity at the chunk joins",
              step_chunked <= step_whole + 20,
              "step whole " + std::to_string(step_whole) + ", chunked " + std::to_string(step_chunked));
    }

    // ── Degenerate input must not crash or invent audio ──────────────────────
    {
        PcmConverter c(48000, 2, 48000);
        EqInt("empty in, empty out", (long long)c.Convert(nullptr, 0).size(), 0);
        const std::vector<int16_t> odd = {5};   // half a stereo frame
        EqInt("partial frame is dropped", (long long)c.Convert(odd.data(), odd.size()).size(), 0);
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
