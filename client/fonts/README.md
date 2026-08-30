# Bundled typefaces

⚠️ **Bundled, not assumed present.** BRAND.md warns about exactly this: a logo that names a
font relies on the viewer having it, and falls back to something generic when they do not.
The same applies to an application — a panel that renders in a substitute face is not the
panel that was designed.

| face | weights | role (BRAND.md) |
|---|---|---|
| Barlow Condensed | SemiBold 600, Bold 700 | display — headings, section labels, uppercase with letter-spacing |
| IBM Plex Sans | Regular 400, Medium 500 | body |
| IBM Plex Mono | Regular 400, Medium 500 | data — frequencies, measurements, anything numeric |

All three are SIL Open Font Licence 1.1, which permits bundling. `OFL.txt` is the licence
text; it must ship with any binary that embeds these.

Source: the Google Fonts repository (`github.com/google/fonts`), which is the upstream for
all three.
