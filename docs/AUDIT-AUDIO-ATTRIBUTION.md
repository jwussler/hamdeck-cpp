# Audio attribution: what NetLogger and the logbook can actually tell us

Measured 09/01/2026 against the live NetLogger API and the 29,578-QSO logbook on the
wavelog-test rig. Nothing here is inferred from documentation alone.

## The wall: NetLogger records WHO, never WHEN

A `<Checkin>` carries `SerialNo, Callsign, Status, FirstName, PreferredName, Street,
CityCountry, State, Zip, Country, County, Grid, DXCC, MemberID, Remarks, QSLInfo`.
Pulled live from `GetCheckins.php`, every field listed. **There is no timestamp on a
check-in, and no per-station time anywhere in the API.** `GetPastNetCheckins` returns the
final roster only.

So a recording cannot be segmented from NetLogger history. That is the same shape as the
7-day wall in [[netlogger-xml-api]]: whatever we want, we capture live or we never have it.

## The one live signal: `<Pointer>`

Spec v1.3 line 59, verbatim: **"`<Pointer>` is the SerialNo of the currently working
station."** Returned on every `GetCheckins` call. Observed live: `Pointer=19` of
`CheckinCount=20`.

⚠️ **The pointer is net control's cursor, not a transmit detector.** It moves when the
operator running the net clicks a station. It lags, it can sit still through a long
exchange, and on a loosely-run net it may not move at all. It is a strong hint about who
is being worked; it is not ground truth about who is making noise. Treat it as evidence,
never as a fact — and keep the raw samples so a better rule can be applied later without
re-recording.

Rate limit is **3 GetCheckins/min = one sample per 20 seconds**, which is also the
boundary precision. Do not shorten it; v1.2 added server-side anti-flooding.

## The logbook is the better index — second resolution, genuinely ragged

`COL_TIME_ON` on net-tagged QSOs (12,001 rows via `qsl_qso_net`):

- **98.3% carry non-zero seconds.** These are not minute-rounded stamps.
- **753 net sessions; exactly 1 has every QSO at one identical time (0.1%).**
  Bulk-logging-at-the-end is not what happens.

### ⚠️ The duplicate trap that nearly produced the wrong design
The first spacing measurement said median gap **0 seconds**, 70.2% under 20s — which would
have meant the timestamps were useless for slicing. That was too tidy to be true.
**5,900 of 5,902 zero-gap pairs are the SAME CALLSIGN** — the known duplicate-QSO problem
(5,320 groups logged under two station profiles). The duplicates, not the logging, made
the median zero.

Deduplicated on call+time, the real distribution:

    n=2998   p10=17s   median=211s   p90=1641s   under_20s=11.5%

**A 3.5-minute median between consecutive net QSOs.** That is a sliceable timeline, and it
is a far finer index than the 20-second pointer poll.

By year, undeduplicated, the artifact is visible directly — 2025 and 2026 (live-logged via
the NetLogger sync) show medians of 206s and 63s and only ~13% under 20s, while every year
2019-2024 reads median 0. The older years are duplicate-polluted, not differently logged.

## What this means for the build

1. **Attribution is captured live or not at all.** A recorder that runs without a
   simultaneous pointer/roster capture produces audio that can never be attributed.
2. **The logbook timestamp is the primary index; the pointer track is corroboration.**
   Where they disagree, neither is automatically right and the segment should say so
   rather than pick a winner silently.
3. **Keep the raw poll responses**, not just the derived segments. The segmentation rule
   will change; the recordings and the XML are what was paid for.
4. Segment boundaries are **uncertain by construction** — ±20s at best from the pointer,
   and a logbook stamp marks when a contact was logged, not when the audio started. Any
   page built on this must show that as a range, never as a precise clip.

## Not established

- Whether the pointer actually tracks transmissions closely enough to be useful. That
  needs one real net recorded with the track running, then listened to against it.
  **Nothing here proves it does.**
- Joe's own logging latency: how long after an exchange he commits the row. That offset is
  the single biggest term in the slicing error and it has not been measured.

Related: `qsl-card-system` (the QR spot on the card is the consumer of this),
[[netlogger-xml-api]], and section 1 of docs/internal/CARRYOVER.md for the recorder itself.
