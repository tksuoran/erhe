#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::asset_store {

extern std::shared_ptr<spdlog::logger> log_asset_store;

void initialize_logging();

}
