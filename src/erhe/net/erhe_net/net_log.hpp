#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::net
{

extern std::shared_ptr<spdlog::logger> log_net;
extern std::shared_ptr<spdlog::logger> log_socket;
extern std::shared_ptr<spdlog::logger> log_client;
extern std::shared_ptr<spdlog::logger> log_server;

void initialize_logging();

} // namespace erhe::net
