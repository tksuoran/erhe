#include "erhe_asset_store/asset_store_log.hpp"
#include "erhe_log/log.hpp"

namespace erhe::asset_store {

std::shared_ptr<spdlog::logger> log_asset_store;

void initialize_logging()
{
    using namespace erhe::log;
    log_asset_store = make_logger("erhe.asset_store");
}

}
