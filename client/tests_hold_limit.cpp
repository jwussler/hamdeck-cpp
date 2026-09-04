// The hold limit, and the lost release it exists for.
//
// ⚠️ WRITTEN BEFORE THE PLATFORM CODE IT GUARDS, because the failure it covers
// cannot be reproduced on demand on the machine that ships it. A system-wide
// PTT gets its press from the OS and its release from a second mechanism - a
// key-state poll on Windows, an event on macOS whose delivery is thinly
// documented - and if that release never arrives, the carrier stays up.
//
// The thing being asserted is deliberately narrow: a press with no release
// unkeys after the limit, and a normal over is NOT cut short. Those two pull in
// opposite directions and getting the second one wrong is worse - it cuts an
// operator off mid-sentence on the air, which is a real transmission ruined
// rather than a fault avoided.
//
// ⚠️ The limit is 150 s in the app, under the host's 180 s watchdog, so the two
// cannot race and the app can say which one fired. The timings here are scaled
// down; what is tested is the ORDER and the CONDITIONS, not the constant.

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void Check(const char* what, bool ok) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

// The same shape as Backend's: a press starts the limit, a release stops it,
// the timeout unkeys once.
class HoldGuard : public QObject {
 public:
  explicit HoldGuard(int limit_ms) {
    limit_.setSingleShot(true);
    limit_.setInterval(limit_ms);
    connect(&limit_, &QTimer::timeout, this, [this] {
      unkeys.push_back("limit");
      keyed = false;
    });
  }
  void Pressed() { keyed = true; limit_.start(); }
  void Released() {
    limit_.stop();
    // ⚠️ Only unkeys if it is still keyed. A release arriving AFTER the limit
    // already fired must not send a second off to a rig somebody else may have
    // keyed in between.
    if (keyed) { unkeys.push_back("release"); keyed = false; }
  }
  bool keyed = false;
  std::vector<const char*> unkeys;

 private:
  QTimer limit_;
};

void Pump(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  setvbuf(stdout, nullptr, _IONBF, 0);

  // 1. A normal over: released well inside the limit, and NOT cut short.
  {
    HoldGuard g(300);
    g.Pressed();
    Pump(100);
    Check("a normal over is still keyed part way through", g.keyed);
    g.Released();
    Pump(350);
    Check("it unkeys on the release, not on the limit",
          g.unkeys.size() == 1 && g.unkeys[0] == std::string("release"));
    Check("and the limit does not fire afterwards", g.unkeys.size() == 1);
  }

  // 2. ⚠️ THE LOST RELEASE. The press arrives, the release never does - the
  // exact macOS uncertainty and the exact Windows sleep case - and the carrier
  // must not stay up.
  {
    HoldGuard g(300);
    g.Pressed();
    Pump(150);
    Check("still keyed before the limit", g.keyed);
    Pump(250);
    Check("a lost release unkeys at the limit", !g.keyed);
    Check("and it says the limit did it, not the operator",
          g.unkeys.size() == 1 && g.unkeys[0] == std::string("limit"));
  }

  // 3. A release that turns up LATE, after the limit already unkeyed. It must
  // not send a second off - by then the transmitter may belong to someone else.
  {
    HoldGuard g(200);
    g.Pressed();
    Pump(300);
    Check("the limit fired", g.unkeys.size() == 1);
    g.Released();
    Check("a late release does not unkey a second time", g.unkeys.size() == 1);
  }

  std::printf("%s\n", failures == 0 ? "HOLD LIMIT PASSED" : "HOLD LIMIT FAILED");
  return failures == 0 ? 0 : 1;
}
