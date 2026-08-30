#pragma once

// ⚠️ NEVER `assert()` IN THESE TESTS, AND NEVER PUT A CALL INSIDE ONE.
//
// CI builds RelWithDebInfo, which defines NDEBUG, which compiles `assert(expr)`
// away ENTIRELY - expression included. `assert(q.Pop(out));` did not merely stop
// checking: the Pop never happened, so the next line read `out[0]` on an empty
// vector and the test SEGFAULTED on the runner while passing on the build VM,
// which builds without a build type and therefore keeps asserts live.
//
// That is the section-6 rule in its purest form: for six pushes CI ran 154
// assertions that had been deleted by the preprocessor, and the only reason
// anybody noticed is that one of them crashed rather than quietly passing.
//
// CHECK always evaluates its expression, in every build type, and says which
// file and line failed rather than aborting with a bare signal.

#include <cstdio>
#include <cstdlib>

#define CHECK(expr)                                                       \
  do {                                                                    \
    if (!(expr)) {                                                        \
      std::fprintf(stderr, "CHECK FAILED: %s\n  at %s:%d\n", #expr,       \
                   __FILE__, __LINE__);                                   \
      std::fflush(stderr);                                                \
      std::abort();                                                       \
    }                                                                     \
  } while (0)
