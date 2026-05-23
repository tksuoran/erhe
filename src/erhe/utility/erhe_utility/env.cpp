#include "erhe_utility/env.hpp"

#include <cstdlib>

namespace erhe::utility {

bool set_env(const char* key, const char* value)
{
#if defined(_WIN32)
    return _putenv_s(key, value) == 0;
#else
    return setenv(key, value, 1) == 0;
#endif
}

} // namespace erhe::utility
