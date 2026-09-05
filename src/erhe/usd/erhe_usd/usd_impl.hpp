#pragma once

// Internal header: it includes LightUSD headers and is included only by
// erhe::usd's own translation units, never by a client of erhe::usd.

#include "erhe_usd/usd.hpp"

#include "stage.hh"

#include <filesystem>

namespace erhe::usd {

class Stage::Impl final
{
public:
    lightusd::Stage       stage;
    std::filesystem::path source_path;
};

} // namespace erhe::usd
