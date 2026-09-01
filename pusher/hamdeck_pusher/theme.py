"""Brand tokens, verbatim from ~/hamdeck-site/brand/BRAND.md.

⚠️ Do not invent colours or substitute typefaces. Single dark theme, deliberately - this
is an instrument face and there is no light mode, so every background is painted
explicitly rather than inherited.
"""

GROUND      = "#0E1013"   # panel face
PANEL       = "#171A1F"   # raised tiles
PANEL_DEEP  = "#141619"   # the app's window background
LINE        = "#2A3038"   # hairlines
TEXT        = "#E8EAED"
DIM         = "#8A929C"   # silkscreen labels
AMBER       = "#FFB020"   # THE accent - the VFO digits. There is only one amber.
AMBER_DIM   = "#8A6320"
CYAN        = "#3B82F6"
CYAN_FILL   = "#1E3A6B"
TX_RED      = "#B4232A"
OK_GREEN    = "#32C765"

#: Condensed uppercase is the silkscreen on radio gear - labels only, never paragraphs.
DISPLAY = ("Bahnschrift Condensed", "Barlow Condensed", "Arial Narrow", "DejaVu Sans", "TkDefaultFont")
BODY    = ("Segoe UI", "IBM Plex Sans", "DejaVu Sans", "TkDefaultFont")
#: Anything numeric. Frequencies that jitter as they change look broken, so this must be
#: a real monospace face.
MONO    = ("Consolas", "IBM Plex Mono", "DejaVu Sans Mono", "TkFixedFont")


def pick(families, wanted) -> str:
    """First of `wanted` that the system actually has. Never assume a font is present."""
    have = {f.lower() for f in families}
    for name in wanted:
        if name.lower() in have:
            return name
    return wanted[-1]
