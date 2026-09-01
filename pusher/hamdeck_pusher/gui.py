"""The window.

⚠️ IT ANSWERS ONE QUESTION: is my log following my radio?

Everything else is secondary, so the screen is two readouts side by side - what the RADIO
says and what is IN THE LOG - and a single line of plain English underneath saying why
they do or do not match. A status light that only reports "running" is precisely what this
project exists to stop shipping: deferring, a rejected API key and a frozen reading all
look identical from Wavelog, and they must not look identical here.

Brand tokens come from theme.py, verbatim. Tk only - no third-party GUI dependency, so the
licence surface stays clean for code signing.
"""

from __future__ import annotations

import queue
import threading
import tkinter as tk
from tkinter import font as tkfont
from tkinter import messagebox

from . import theme as T
from .config import Settings, default_path
from .runner import Runner
from .state import Phase, State

#: What each phase means to a human, and what colour to paint it.
PHASE_LOOK = {
    Phase.PUBLISHING: (T.OK_GREEN,  "LOGGING"),
    Phase.IDLE:       (T.CYAN,      "WATCHING"),
    Phase.DEFERRED:   (T.AMBER,     "STANDING BY"),
    Phase.STALE:      (T.AMBER,     "STALE"),
    Phase.RIG_DOWN:   (T.TX_RED,    "NO RADIO"),
    Phase.NO_HOST:    (T.TX_RED,    "NO HOST"),
    Phase.FAILING:    (T.TX_RED,    "NOT LOGGING"),
}


def _fmt_freq(hz: int | None) -> str:
    # Tabular digits and a fixed shape: a frequency that changes width as it changes
    # value reads as broken.
    return "—.———.—" if not hz else f"{hz/1e6:9.4f}"


class App:
    def __init__(self, root: tk.Tk, settings: Settings, config_path):
        self.root = root
        self.settings = settings
        self.config_path = config_path
        self.updates: queue.Queue[State] = queue.Queue()
        self.runner: Runner | None = None
        self.thread: threading.Thread | None = None

        fams = tkfont.families(root)
        self.f_label = (T.pick(fams, T.DISPLAY), 10, "bold")
        self.f_body  = (T.pick(fams, T.BODY), 10)
        self.f_freq  = (T.pick(fams, T.MONO), 26, "bold")
        self.f_mode  = (T.pick(fams, T.MONO), 12)
        self.f_state = (T.pick(fams, T.DISPLAY), 15, "bold")

        root.title("HamDeck — Wavelog")
        root.configure(bg=T.PANEL_DEEP)
        root.minsize(520, 300)
        self._build()
        self.root.after(200, self._drain)

    # ── layout ────────────────────────────────────────────────────────────────
    def _readout(self, parent, caption: str, accent: str):
        card = tk.Frame(parent, bg=T.PANEL, highlightbackground=T.LINE,
                        highlightthickness=1)
        tk.Label(card, text=caption, bg=T.PANEL, fg=T.DIM, font=self.f_label).pack(
            anchor="w", padx=12, pady=(9, 0))
        freq = tk.Label(card, text=_fmt_freq(None), bg=T.PANEL, fg=accent, font=self.f_freq)
        freq.pack(anchor="w", padx=12)
        mode = tk.Label(card, text="", bg=T.PANEL, fg=T.DIM, font=self.f_mode)
        mode.pack(anchor="w", padx=12, pady=(0, 10))
        return card, freq, mode

    def _build(self):
        head = tk.Frame(self.root, bg=T.PANEL_DEEP)
        head.pack(fill="x", padx=16, pady=(14, 4))
        tk.Label(head, text="WAVELOG DATA PUSHER", bg=T.PANEL_DEEP, fg=T.TEXT,
                 font=(self.f_label[0], 12, "bold")).pack(side="left")
        self.radio_lbl = tk.Label(head, text="", bg=T.PANEL_DEEP, fg=T.DIM,
                                  font=self.f_body)
        self.radio_lbl.pack(side="right")

        row = tk.Frame(self.root, bg=T.PANEL_DEEP)
        row.pack(fill="x", padx=16, pady=6)
        # ⚠️ RADIO on the left, LOG on the right, always both. One number cannot show a
        # divergence, and the divergence is the entire point of the app.
        c1, self.rig_freq, self.rig_mode = self._readout(row, "RADIO", T.AMBER)
        c1.pack(side="left", fill="both", expand=True, padx=(0, 5))
        c2, self.log_freq, self.log_mode = self._readout(row, "IN THE LOG", T.AMBER_DIM)
        c2.pack(side="left", fill="both", expand=True, padx=(5, 0))

        bar = tk.Frame(self.root, bg=T.PANEL, highlightbackground=T.LINE,
                       highlightthickness=1)
        bar.pack(fill="x", padx=16, pady=(4, 6))
        self.lamp = tk.Canvas(bar, width=12, height=12, bg=T.PANEL, highlightthickness=0)
        self.lamp.pack(side="left", padx=(12, 8), pady=12)
        self.lamp_dot = self.lamp.create_oval(1, 1, 11, 11, fill=T.DIM, outline="")
        self.state_lbl = tk.Label(bar, text="STARTING", bg=T.PANEL, fg=T.DIM,
                                  font=self.f_state)
        self.state_lbl.pack(side="left")

        self.why = tk.Label(self.root, text="", bg=T.PANEL_DEEP, fg=T.DIM,
                            font=self.f_body, anchor="w", justify="left", wraplength=480)
        self.why.pack(fill="x", padx=18, pady=(0, 4))

        foot = tk.Frame(self.root, bg=T.PANEL_DEEP)
        foot.pack(fill="x", side="bottom", padx=16, pady=12)
        self._button(foot, "Settings", self.open_settings).pack(side="left")
        self.toggle_btn = self._button(foot, "Stop", self.toggle)
        self.toggle_btn.pack(side="right")

    def _button(self, parent, text, cmd):
        return tk.Button(parent, text=text, command=cmd, bg=T.PANEL, fg=T.TEXT,
                         activebackground=T.CYAN_FILL, activeforeground=T.TEXT,
                         relief="flat", font=self.f_body, padx=14, pady=5,
                         highlightbackground=T.LINE, bd=0)

    # ── the loop ──────────────────────────────────────────────────────────────
    def start(self):
        if self.runner:
            return
        problems = self.settings.problems()
        if problems:
            self._paint_problem("NOT CONFIGURED", "; ".join(problems))
            return
        self.runner = Runner(self.settings)
        self.thread = threading.Thread(
            target=self.runner.run, kwargs={"on_change": self.updates.put}, daemon=True)
        self.thread.start()
        self.toggle_btn.config(text="Stop")

    def stop(self):
        if self.runner:
            self.runner.stop()
            self.runner = None
        self.toggle_btn.config(text="Start")

    def toggle(self):
        self.stop() if self.runner else self.start()

    def _drain(self):
        try:
            while True:
                self._paint(self.updates.get_nowait())
        except queue.Empty:
            pass
        self.root.after(200, self._drain)

    # ── painting ──────────────────────────────────────────────────────────────
    def _paint_problem(self, label: str, why: str):
        self.lamp.itemconfig(self.lamp_dot, fill=T.TX_RED)
        self.state_lbl.config(text=label, fg=T.TX_RED)
        self.why.config(text=why)

    def _paint(self, st: State):
        colour, label = PHASE_LOOK.get(st.phase, (T.DIM, st.phase.value.upper()))
        self.lamp.itemconfig(self.lamp_dot, fill=colour)
        self.state_lbl.config(text=label, fg=colour)
        self.radio_lbl.config(text=self.settings.radio_name)

        self.rig_freq.config(text=_fmt_freq(st.observed_freq))
        self.rig_mode.config(text=(st.observed_mode or "").upper())
        self.log_freq.config(text=_fmt_freq(st.published_freq))
        self.log_mode.config(text=(st.published_mode or "").upper())
        # ⚠️ The log readout goes AMBER when it matches the radio and DIM when it does
        # not, so "the log is behind" is visible without reading a word.
        self.log_freq.config(fg=T.AMBER if st.in_sync else T.AMBER_DIM)

        why = st.reason
        age = st.age_seconds()
        if st.published_freq is None:
            why += " — nothing logged yet this session"
        elif not st.in_sync and age is not None:
            why += f" — the log is {int(age)}s behind the radio"
        self.why.config(text=why)

    # ── settings ──────────────────────────────────────────────────────────────
    def open_settings(self):
        SettingsDialog(self.root, self)


class SettingsDialog:
    FIELDS = [
        ("host_url", "HOST URL", False), ("host_user", "HOST USER", False),
        ("host_password", "HOST PASSWORD", True),
        ("wavelog_url", "WAVELOG URL", False), ("wavelog_key", "WAVELOG API KEY", True),
        ("radio_name", "RADIO NAME (Wavelog's key for the row)", False),
    ]

    def __init__(self, parent, app: App):
        self.app = app
        self.win = tk.Toplevel(parent)
        self.win.title("Settings")
        self.win.configure(bg=T.PANEL_DEEP)
        self.win.transient(parent)
        self.win.grab_set()
        self.vars = {}
        for key, label, secret in self.FIELDS:
            tk.Label(self.win, text=label, bg=T.PANEL_DEEP, fg=T.DIM,
                     font=app.f_label).pack(anchor="w", padx=16, pady=(10, 2))
            v = tk.StringVar(value=getattr(app.settings, key))
            e = tk.Entry(self.win, textvariable=v, bg=T.PANEL, fg=T.TEXT, width=46,
                         insertbackground=T.TEXT, relief="flat", font=app.f_body,
                         show="*" if secret else "")
            e.pack(anchor="w", padx=16, ipady=4)
            self.vars[key] = v
        self.defer = tk.BooleanVar(value=app.settings.defer_to_remote)
        tk.Checkbutton(self.win, text="Stand down while the remote client is operating",
                       variable=self.defer, bg=T.PANEL_DEEP, fg=T.TEXT,
                       selectcolor=T.PANEL, activebackground=T.PANEL_DEEP,
                       activeforeground=T.TEXT, font=app.f_body).pack(
            anchor="w", padx=13, pady=(12, 4))
        row = tk.Frame(self.win, bg=T.PANEL_DEEP)
        row.pack(fill="x", padx=16, pady=14)
        app._button(row, "Save & restart", self.save).pack(side="right")
        app._button(row, "Cancel", self.win.destroy).pack(side="right", padx=(0, 8))

    def save(self):
        for key, v in self.vars.items():
            setattr(self.app.settings, key, v.get().strip())
        self.app.settings.defer_to_remote = self.defer.get()
        problems = self.app.settings.problems()
        if problems:
            messagebox.showerror("Not saved", "\n".join(problems), parent=self.win)
            return
        self.app.settings.save(self.app.config_path)
        self.win.destroy()
        # ⚠️ A settings change must reach the RUNNING loop. Saving a file and leaving the
        # old values live is how somebody fixes an API key and watches it keep failing.
        self.app.stop()
        self.app.start()


def run(config_path=None, shot=None, shot_ms: int = 1500) -> int:
    path = config_path or default_path()
    root = tk.Tk()
    app = App(root, Settings.load(path), path)
    app.start()
    root.protocol("WM_DELETE_WINDOW", lambda: (app.stop(), root.destroy()))
    if shot is not None:
        # A rendering pass for looking at the real thing, rather than reasoning about it.
        root.after(shot_ms, lambda: (app.stop(), root.destroy()))
    root.mainloop()
    return 0
