#include "global_hotkey.h"

#include <QGuiApplication>
#include <QStringList>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Choice {
  const char* label;
  unsigned modifiers;
  unsigned vk;
};

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
const Choice kChoices[] = {
    {"Off", 0, 0},
    {"Ctrl+Alt+Space", kModControl | kModAlt, 0x20},
    {"Ctrl+Alt+P", kModControl | kModAlt, 0x50},
    {"Ctrl+Shift+Space", kModControl | kModShift, 0x20},
    {"Pause", 0, 0x13},
    {"Scroll Lock", 0, 0x91},
    {"F13", 0, 0x7C},
};

const Choice* Find(const QString& label) {
  for (const auto& c : kChoices) {
    if (label == QString::fromLatin1(c.label)) return &c;
  }
  return nullptr;
}

}  // namespace

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey() { Unregister(); }

QStringList GlobalHotkey::Choices() {
  QStringList out;
  for (const auto& c : kChoices) out << QString::fromLatin1(c.label);
  return out;
}

#ifdef _WIN32

QString GlobalHotkey::Apply(const QString& label, QWindow* window) {
  Unregister();
  label_ = label;

  const Choice* choice = Find(label);
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
  BOOL ok = RegisterHotKey(h, kHotkeyId, choice->modifiers | kModNoRepeat, choice->vk);
  DWORD err = ok ? 0 : GetLastError();

  // ⚠️ MOD_NOREPEAT is not honoured everywhere for a key with NO other
  // modifier. Fall back rather than leave the operator with a dead key: a
  // repeating toggle is still better than nothing, and it is obvious the moment
  // it happens.
  if (!ok && choice->modifiers == 0) {
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
  return {};
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
  if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
  auto* msg = static_cast<MSG*>(message);
  if (msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kHotkeyId) {
    ++press_count_;
    emit Pressed();
    return true;
  }
  return false;
}

void GlobalHotkey::Unregister() {
  if (!armed_ || !hwnd_) return;
  UnregisterHotKey(static_cast<HWND>(hwnd_), kHotkeyId);
  armed_ = false;
}

#else

// ⚠️ NOT WRITTEN FOR X11 OR macOS, AND IT SAYS SO RATHER THAN PRETENDING.
// X11 needs XGrabKey per keycode and fights desktop environments that already
// grabbed the combination; macOS needs Accessibility permission granted by
// hand. Reporting "armed" here would be the same class of lie as a status route
// that reports ok for something it never did - the window-focus hotkey still
// works on both.
QString GlobalHotkey::Apply(const QString& label, QWindow*) {
  label_ = label;
  armed_ = false;
  const Choice* choice = Find(label);
  if (!choice || choice->vk == 0) return {};
  return "a system-wide hotkey needs platform code that is only written for "
         "Windows - the window-focus key still works";
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void*, qintptr*) { return false; }

void GlobalHotkey::Unregister() { armed_ = false; }

#endif
