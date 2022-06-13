#include "erhe/graphics/pipeline.hpp"

#include <vector>

namespace erhe::graphics
{

std::mutex             Pipeline::s_mutex;
std::vector<Pipeline*> Pipeline::s_pipelines;

Pipeline::Pipeline()
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_pipelines.push_back(this);
}

Pipeline::Pipeline(Pipeline_data&& create_info)
    : data{std::move(create_info)}
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_pipelines.push_back(this);
}

Pipeline::Pipeline(const Pipeline& other)
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    s_pipelines.push_back(this);
    data = other.data;
}

auto Pipeline::operator=(const Pipeline& other) -> Pipeline&
{
    data = other.data;
    return *this;
}

Pipeline::~Pipeline() noexcept
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    const auto i = std::remove(s_pipelines.begin(), s_pipelines.end(), this);
    if (i != s_pipelines.end())
    {
        s_pipelines.erase(i, s_pipelines.end());
    }
}

auto Pipeline::get_pipelines() -> std::vector<Pipeline*>
{
    const std::lock_guard<std::mutex> lock{s_mutex};

    return s_pipelines;
}

} // namespace erhe::graphics
