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

auto is_deterministic_clock() -> bool
{
    static const bool s_deterministic_clock = []() {
        const char* const value = std::getenv("ERHE_DETERMINISTIC_CLOCK");
        return (value != nullptr) && (value[0] == '1');
    }();
    return s_deterministic_clock;
}

auto is_default_layout() -> bool
{
    static const bool s_default_layout = []() {
        const char* const value = std::getenv("ERHE_DEFAULT_LAYOUT");
        return (value != nullptr) && (value[0] == '1');
    }();
    return s_default_layout;
}

}
