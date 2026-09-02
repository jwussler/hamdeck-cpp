# Internal engineering notes

Working notes, not documentation. They are written to whoever is next at this
keyboard — candid, dated, full of things that turned out to be wrong, and
addressed to the operator by name in places.

They are kept public deliberately. The reason a fix worked, and the measurement
that proved it, is more useful than a tidy summary that leaves out how long it
took to find. Several of these files exist because a green build lied.

| file | what it is |
|---|---|
| `CARRYOVER.md` | the API surface, the audio chain with measured numbers, and a list of things that are **not possible** so nobody retries them |
| `WIP.md` | running build log — every trap, in the order it bit |
| `AUDIT-CSHARP.md` | walking the C# implementation down before writing anything |
| `AUDIT-WAVELOG.md` | the same for the Wavelog bridge |
| `DAY-08-30-2026.md` | one day's account, kept because the failure modes repeat |

⚠️ Nothing station-specific belongs in this repository — no hostnames,
addresses, VM ids or tunnel details. Site detail lives in a gitignored
`SITE.md`. That applies to commit messages too.
