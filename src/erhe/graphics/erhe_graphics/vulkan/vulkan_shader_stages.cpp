#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::graphics {

using std::string;

namespace {

auto create_shader_module(
    Device&                       device,
    std::span<const unsigned int> spirv,
    const std::string&            shader_name,
    const char*                   stage_name
) -> VkShaderModule
{
    if (spirv.empty()) {
        return VK_NULL_HANDLE;
    }

    VkDevice vulkan_device = device.get_impl().get_vulkan_device();

    const VkShaderModuleCreateInfo create_info{
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext    = nullptr,
        .flags    = 0,
        .codeSize = spirv.size() * sizeof(unsigned int),
        .pCode    = spirv.data()
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(vulkan_device, &create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        log_program->error(
            "vkCreateShaderModule() failed for {} stage of '{}' with {}",
            stage_name, shader_name, static_cast<int32_t>(result)
        );
        return VK_NULL_HANDLE;
    }

    device.get_impl().set_debug_label(
        VK_OBJECT_TYPE_SHADER_MODULE,
        reinterpret_cast<uint64_t>(shader_module),
        fmt::format("{} {}", shader_name, stage_name)
    );

    log_program->info(
        "VkShaderModule created for {} stage of '{}' ({} bytes SPIR-V)",
        stage_name, shader_name, spirv.size() * sizeof(unsigned int)
    );

    return shader_module;
}

} // anonymous namespace

Shader_stages_impl::Shader_stages_impl(Device& device, const std::string& failed_name)
    : m_device{device}
{
    m_name = fmt::format("{} - compilation failed", failed_name);
    ERHE_VERIFY(!failed_name.empty());
}

Shader_stages_impl::Shader_stages_impl(Shader_stages_impl&& from)
    : m_device         {from.m_device}
    , m_name           {std::move(from.m_name)}
    , m_is_valid       {from.m_is_valid}
    , m_vertex_module  {from.m_vertex_module}
    , m_fragment_module{from.m_fragment_module}
    , m_compute_module {from.m_compute_module}
{
    from.m_is_valid        = false;
    from.m_vertex_module   = VK_NULL_HANDLE;
    from.m_fragment_module = VK_NULL_HANDLE;
    from.m_compute_module  = VK_NULL_HANDLE;
}

Shader_stages_impl& Shader_stages_impl::operator=(Shader_stages_impl&& from)
{
    if (this != &from) {
        destroy_modules();
        m_name            = std::move(from.m_name);
        m_is_valid        = from.m_is_valid;
        m_vertex_module   = from.m_vertex_module;
        m_fragment_module = from.m_fragment_module;
        m_compute_module  = from.m_compute_module;
        from.m_is_valid        = false;
        from.m_vertex_module   = VK_NULL_HANDLE;
        from.m_fragment_module = VK_NULL_HANDLE;
        from.m_compute_module  = VK_NULL_HANDLE;
    }
    return *this;
}

Shader_stages_impl::Shader_stages_impl(Device& device, Shader_stages_prototype&& prototype)
    : m_device{device}
{
    reload(std::move(prototype));
}

void Shader_stages_impl::destroy_modules()
{
    VkDevice vulkan_device = m_device.get_impl().get_vulkan_device();
    if (m_vertex_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan_device, m_vertex_module, nullptr);
        m_vertex_module = VK_NULL_HANDLE;
    }
    if (m_fragment_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan_device, m_fragment_module, nullptr);
        m_fragment_module = VK_NULL_HANDLE;
    }
    if (m_compute_module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vulkan_device, m_compute_module, nullptr);
        m_compute_module = VK_NULL_HANDLE;
    }
}

auto Shader_stages_impl::is_valid() const -> bool
{
    return m_is_valid;
}

void Shader_stages_impl::invalidate()
{
    m_is_valid = false;
    destroy_modules();
}

auto Shader_stages_impl::name() const -> const std::string&
{
    return m_name;
}

void Shader_stages_impl::reload(Shader_stages_prototype&& prototype)
{
    if (!prototype.is_valid()) {
        invalidate();
        m_name = prototype.name();
        return;
    }

    destroy_modules();

    m_name = prototype.name();

    Shader_stages_prototype_impl& proto_impl = prototype.get_impl();

    m_vertex_module = create_shader_module(
        m_device,
        proto_impl.get_spirv_binary(Shader_type::vertex_shader),
        m_name,
        "vertex"
    );

    m_fragment_module = create_shader_module(
        m_device,
        proto_impl.get_spirv_binary(Shader_type::fragment_shader),
        m_name,
        "fragment"
    );

    m_compute_module = create_shader_module(
        m_device,
        proto_impl.get_spirv_binary(Shader_type::compute_shader),
        m_name,
        "compute"
    );

    if ((m_vertex_module == VK_NULL_HANDLE) && (m_compute_module == VK_NULL_HANDLE)) {
        log_program->error("No vertex or compute shader module for: {}", m_name);
        m_is_valid = false;
        return;
    }

    m_is_valid = true;
}

auto Shader_stages_impl::get_vertex_module() const -> VkShaderModule
{
    return m_vertex_module;
}

auto Shader_stages_impl::get_fragment_module() const -> VkShaderModule
{
    return m_fragment_module;
}

auto Shader_stages_impl::get_compute_module() const -> VkShaderModule
{
    return m_compute_module;
}

auto operator==(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    return &lhs == &rhs;
}

auto operator!=(const Shader_stages_impl& lhs, const Shader_stages_impl& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

void Shader_stages_tracker::reset()
{
}

void Shader_stages_tracker::execute(const Shader_stages* state)
{
    static_cast<void>(state);
}

} // namespace erhe::graphics
