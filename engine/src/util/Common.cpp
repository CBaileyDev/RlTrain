#include "Common.h"

#include <cstdio>
#include <cstdlib>

namespace rls::detail {

[[noreturn]] void FailVerify(const char* expression, const char* message,
                           const char* file, int line) noexcept {
    std::fprintf(stderr, "%s:%d: verification failed: %s (%s)\n",
                 file, line, expression, message);
    std::abort();
}

} // namespace rls::detail
