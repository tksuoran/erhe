#include "prefabs/prefab_instance.hpp"

namespace editor {

Prefab_instance::Prefab_instance()
    : Item{"prefab instance"}
{
}

Prefab_instance::Prefab_instance(const std::filesystem::path& source_path, const std::string& prefab_name)
    : Item                 {prefab_name}
    , m_prefab_source_path {source_path}
    , m_prefab_name        {prefab_name}
{
}

Prefab_instance::Prefab_instance(const Prefab_instance& src, erhe::for_clone)
    : Item                 {src, erhe::for_clone{}}
    , m_prefab_source_path {src.m_prefab_source_path}
    , m_prefab_name        {src.m_prefab_name}
{
}

Prefab_instance::Prefab_instance(const Prefab_instance&)            = default;
Prefab_instance& Prefab_instance::operator=(const Prefab_instance&) = default;
Prefab_instance::~Prefab_instance() noexcept                        = default;

auto Prefab_instance::get_prefab_source_path() const -> const std::filesystem::path&
{
    return m_prefab_source_path;
}

auto Prefab_instance::get_prefab_name() const -> const std::string&
{
    return m_prefab_name;
}

}
