#include "ai_driver.hpp"

#include <cstdlib>

namespace editor {

auto is_ai_driver() -> bool
{
    static const bool s_ai_driver = []() {
        const char* const value = std::getenv("ERHE_AI_DRIVER");
        return (value != nullptr) && (value[0] == '1');
    }();
    return s_ai_driver;
}

}
