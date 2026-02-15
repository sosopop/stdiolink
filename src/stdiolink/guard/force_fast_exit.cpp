#include "force_fast_exit.h"
#include <cstdlib>

namespace stdiolink {

void forceFastExit(int code) {
    std::_Exit(code);
}

} // namespace stdiolink
