// The frequency parser, against the reference implementation's own test cases.
//
// ⚠️ THE CASES BELOW ARE COPIED FROM HamDeck.Tests/FreqInputTests.cs, not
// invented. The point of a port is that it gives the same answer, and the only
// way to know that is to run the original's cases against it. Two of them - the
// grouped "7.185.000" form and the "below 100 means MHz" rule - are exactly the
// ones a fresh implementation gets wrong.

#include <QString>
#include <cstdio>
#include <string>

#include "src/freq_input.h"

static int failures = 0;

static void Eq(const char* input, long long want) {
    const long long got = FreqInput::Parse(QString::fromUtf8(input));
    const bool ok = got == want;
    std::printf("  %s Parse(\"%s\") = %lld%s\n", ok ? "ok  " : "FAIL", input, got,
                ok ? "" : (" wanted " + std::to_string(want)).c_str());
    if (!ok) ++failures;
}

int main() {
    // Straight from FreqInputTests.Parse_MatchesTheHost.
    Eq("14.200", 14200000);
    Eq("7.125", 7125000);
    Eq("3.5", 3500000);
    Eq("14200", 14200000);
    Eq("14200000", 14200000);
    Eq("14", 14000000);
    Eq("7185", 7185000);
    Eq("", 0);
    Eq("abc", 0);

    // Parse_StripsCommasAndSpaces
    Eq("14,200,000", 14200000);
    Eq(" 14200000 ", 14200000);

    // Parse_AcceptsTheGroupedFormThePanelDisplays - what the big readout shows,
    // so it is what somebody retypes.
    Eq("7.185.000", 7185000);
    Eq("14.074.000", 14074000);

    // InRange_TracksWhatSetFreqWillAccept
    struct { long long hz; bool want; } kRange[] = {
        {7185000, true}, {30000, true}, {75000000, true},
        {29999, false}, {75000001, false}, {0, false},
    };
    for (const auto& c : kRange) {
        const bool got = FreqInput::InRange(c.hz);
        const bool ok = got == c.want;
        std::printf("  %s InRange(%lld) = %s\n", ok ? "ok  " : "FAIL", c.hz,
                    got ? "true" : "false");
        if (!ok) ++failures;
    }

    // ToEditText_SeedsTheBoxWithSomethingParseable, including the round trip.
    struct { long long hz; const char* want; } kEdit[] = {
        {7185000, "7.185"}, {14074000, "14.074"}, {7185120, "7.18512"},
    };
    for (const auto& c : kEdit) {
        const QString got = FreqInput::ToEditText(c.hz);
        const bool ok = got == QString::fromUtf8(c.want) &&
                        FreqInput::Parse(got) == c.hz;
        std::printf("  %s ToEditText(%lld) = \"%s\" (round trips)\n",
                    ok ? "ok  " : "FAIL", c.hz, got.toUtf8().constData());
        if (!ok) ++failures;
    }

    std::printf(failures ? "FREQ INPUT FAILED\n" : "FREQ INPUT PASSED\n");
    return failures ? 1 : 0;
}
