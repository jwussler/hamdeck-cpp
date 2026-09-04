#include "global_hotkey.h"

#include "ptt_hotkey.h"

#include <QGuiApplication>
#include <QStringList>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
constexpr unsigned kModAlt = MOD_ALT;
constexpr unsigned kModControl = MOD_CONTROL;
constexpr unsigned kModShift = MOD_SHIFT;
// ⚠️ Without MOD_NOREPEAT a held key auto-repeats and a TOGGLE flaps the
// transmitter on and off many times a second.
constexpr unsigned kModNoRepeat = MOD_NOREPEAT;
constexpr int kHotkeyId = 0xB00B;
#else
constexpr unsigned kModAlt = 0x0001, kModControl = 0x0002, kModShift = 0x0004;
constexpr unsigned kModNoRepeat = 0x4000;
#endif

// ⚠️ Ctrl+Alt+Space keeps the spacebar feel of a PTT bar without touching
// Alt+Space itself. Pause and Scroll Lock are here because almost nothing else
// uses them; F13 exists on many keyboards' macro layers and on footswitches.
// ⚠️ THE LIST LIVES IN ptt_hotkey.h NOW, and this file no longer has one of its
// own. Two tables with overlapping labels is what let the in-app key and the
// global key drift apart into two settings nobody could hold in their head.
// Everything here reads the shared HotkeyChoice.

const HotkeyChoice* Find(const QString& label) { return PttHotkeyByLabel(label); }

}  // namespace

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey() { Unregister(); }

// ⚠️ KEPT ONLY SO NOTHING ELSE HAS TO CHANGE ITS CALL. The choices are the
// SHARED list now - one PTT key setting, armed in the window and system-wide.
QStringList GlobalHotkey::Choices() {
  QStringList out;
  for (const auto& c : PttHotkeyChoices()) out << QString::fromLatin1(c.label);
  return out;
}

#ifdef _WIN32

QString GlobalHotkey::Apply(const QString& label, QWindow* window) {
  Unregister();
  label_ = label;

  const HotkeyChoice* choice = Find(label);
  if (!choice || choice->vk == 0) return {};        // "Off" - not an error
  if (!window) return "the window is not ready yet";

  hwnd_ = reinterpret_cast<void*>(window->winId());
  if (!hwnd_) return "the window has no handle yet";

  // ⚠️ INSTALLED ONCE. The filter is not de-duplicated, and two filters mean
  // one press is delivered twice - a TOGGLE would go on and straight back off,
  // and the key would look dead. Unregister() drops the hotkey but deliberately
  // leaves the filter installed, so this flag is what keeps it to one.
  if (!installed_) {
    qApp->installNativeEventFilter(this);
    installed_ = true;
  }

  auto* h = static_cast<HWND>(hwnd_);
  BOOL ok = RegisterHotKey(h, kHotkeyId, choice->mods | kModNoRepeat, choice->vk);
  DWORD err = ok ? 0 : GetLastError();

  // ⚠️ MOD_NOREPEAT is not honoured everywhere for a key with NO other
  // modifier. Fall back rather than leave the operator with a dead key: a
  // repeating toggle is still better than nothing, and it is obvious the moment
  // it happens.
  if (!ok && choice->mods == 0) {
    ok = RegisterHotKey(h, kHotkeyId, 0, choice->vk);
    if (!ok) err = GetLastError();
  }

  if (!ok) {
    armed_ = false;
    // 1409 = ERROR_HOTKEY_ALREADY_REGISTERED. Naming the cause stops the
    // operator hunting a fault in this app that is not in this app.
    return err == 1409
               ? QString("%1 is already taken by another program - pick another").arg(label)
               : QString("could not register %1 (error %2)").arg(label).arg(err);
  }
  armed_ = true;
  vk_ = choice->vk;

  // The poll only lives between a press and its release; see PollKeyState.
  if (!poll_) {
    poll_ = new QTimer(this);
    poll_->setInterval(25);
    connect(poll_, &QTimer::timeout, this, &GlobalHotkey::PollKeyState);
  }
  hold_capable_ = true;
  return {};
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
  if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
  auto* msg = static_cast<MSG*>(message);
  if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kHotkeyId) {
    ++press_count_;
    key_down_ = true;
    emit Pressed();
    // ⚠️ AND NOW WATCH FOR THE RELEASE, which is what makes this hold-to-talk.
    if (poll_) poll_->start();
    return true;
  }
  return false;
}

// ⚠️ THIS IS THE WHOLE TRICK, AND IT REPLACES A CLAIM THIS FILE USED TO MAKE.
//
// RegisterHotKey delivers the DOWN edge only, and the comment below the Windows
// block said for months that hold-to-talk therefore needs a WH_KEYBOARD_LL hook
// - which sees every keystroke on the machine and is a privacy and
// antivirus-flagging problem, so it was refused and the operator got a toggle.
//
// GetAsyncKeyState answers a much narrower question: is THIS ONE key, the one
// the operator nominated, down right now? Polling it after the hotkey fires
// gives the release edge with no hook and no keylogging surface - this can only
// ever learn about the single key it was told about.
//
// 25 ms because it bounds how late an unkey can be, and a late unkey is a tail
// of dead carrier. It runs ONLY between the press and the release, so an idle
// HamDeck polls nothing.
void GlobalHotkey::PollKeyState() {
  if (!key_down_ || vk_ == 0) return;
  if ((GetAsyncKeyState(static_cast<int>(vk_)) & 0x8000) == 0) {
    key_down_ = false;
    if (poll_) poll_->stop();
    emit Released();
  }
}

void GlobalHotkey::Unregister() {
  if (poll_) poll_->stop();
  // ⚠️ A key held while the registration goes away never delivers its release,
  // so the carrier would stay up. Say it went up.
  if (key_down_) {
    key_down_ = false;
    emit Released();
  }
  if (!armed_ || !hwnd_) return;
  UnregisterHotKey(static_cast<HWND>(hwnd_), kHotkeyId);
  armed_ = false;
}

#elif defined(Q_OS_MACOS)

// ⚠️ CARBON, AND ON PURPOSE. RegisterEventHotKey is the ONLY public macOS API
// that registers a system-wide key without the Accessibility or Input Monitoring
// permission - it is narrowly scoped, the app only ever learns that this one
// combination was pressed, and it is what VS Code, Slack and Electron use. The
// header is deprecated and the function is not going anywhere.
//
// ⚠️ AND kEventHotKeyReleased IS THE UNCERTAIN HALF. It is thinly documented,
// and hold-to-talk depends on it entirely. So this asks for BOTH events and only
// claims hold once a release has actually been seen: hold_capable_ starts false
// on this platform and is set by the first release that arrives. Until then the
// panel says "toggle", which is the truth, and the operator is never told they
// have hold-to-talk on the word of a header file.

#include <Carbon/Carbon.h>

namespace {
constexpr UInt32 kSignature = 'HMDK';
}

QString GlobalHotkey::Apply(const QString& label, QWindow*) {
  Unregister();
  label_ = label;
  hold_capable_ = false;

  const HotkeyChoice* choice = Find(label);
  if (!choice) return {};
  if (choice->mac_vk == 0) {
    // Honest, per key: Pause/Break and Scroll Lock are not on a Mac keyboard,
    // and a bare modifier cannot be registered as a hotkey.
    return QString("%1 cannot be a system-wide key on macOS - it still works "
                   "while HamDeck has focus").arg(label);
  }

  if (!handler_installed_) {
    EventTypeSpec types[2] = {{kEventClassKeyboard, kEventHotKeyPressed},
                              {kEventClassKeyboard, kEventHotKeyReleased}};
    InstallApplicationEventHandler(
        [](EventHandlerCallRef, EventRef event, void* ctx) -> OSStatus {
          auto* self = static_cast<GlobalHotkey*>(ctx);
          if (GetEventKind(event) == kEventHotKeyPressed) {
            ++self->press_count_;
            self->key_down_ = true;
            emit self->Pressed();
          } else {
            // ⚠️ THE FIRST RELEASE THAT ARRIVES IS THE PROOF. Only now is hold
            // claimed, and only now does the status line stop saying toggle.
            self->hold_capable_ = true;
            if (self->key_down_) {
              self->key_down_ = false;
              emit self->Released();
            }
          }
          return noErr;
        },
        2, types, this, nullptr);
    handler_installed_ = true;
  }

  EventHotKeyID id{kSignature, 1};
  EventHotKeyRef ref = nullptr;
  const OSStatus rc = RegisterEventHotKey(choice->mac_vk, choice->mods,
                                          id, GetApplicationEventTarget(), 0, &ref);
  if (rc != noErr || ref == nullptr) {
    armed_ = false;
    // ⚠️ eventHotKeyExistsErr is another program holding the key, which is not a
    // fault in this app and the operator cannot be expected to guess it.
    return rc == eventHotKeyExistsErr
               ? QString("%1 is already taken by another program - pick another").arg(label)
               : QString("could not register %1 (OSStatus %2)").arg(label).arg(rc);
  }
  hotkey_ref_ = ref;
  vk_ = choice->mac_vk;
  armed_ = true;
  return {};
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*) { return false; }

void GlobalHotkey::PollKeyState() {}

void GlobalHotkey::Unregister() {
  // ⚠️ A key held while the registration goes away never delivers its release.
  if (key_down_) {
    key_down_ = false;
    emit Released();
  }
  if (hotkey_ref_) {
    UnregisterEventHotKey(static_cast<EventHotKeyRef>(hotkey_ref_));
    hotkey_ref_ = nullptr;
  }
  armed_ = false;
}

#else

// ⚠️ NOT WRITTEN FOR X11, AND IT SAYS SO RATHER THAN PRETENDING.
// X11 needs XGrabKey per keycode and fights desktop environments that already
// grabbed the combination; macOS needs Accessibility permission granted by
// hand. Reporting "armed" here would be the same class of lie as a status route
// that reports ok for something it never did - the window-focus hotkey still
// works on both.
QString GlobalHotkey::Apply(const QString& label, QWindow*) {
  label_ = label;
  armed_ = false;
  hold_capable_ = false;
  const HotkeyChoice* choice = Find(label);
  if (!choice || choice->vk == 0) return {};
  return "a system-wide hotkey needs platform code that is only written for "
         "Windows and macOS - the window-focus key still works";
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*) { return false; }

void GlobalHotkey::PollKeyState() {}

void GlobalHotkey::Unregister() {
  if (key_down_) {
    key_down_ = false;
    emit Released();
  }
  armed_ = false;
}

#endif
