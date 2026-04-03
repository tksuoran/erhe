#include "erhe_graphics/metal/metal_shader_stages.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <Metal/Metal.hpp>

#include <fmt/format.h>
#include <spirv_msl.hpp>

namespace erhe::graphics {

namespace {

auto compile_spirv_to_mtl_function(
    Device&                          device,
    MTL::Device*                     mtl_device,
    std::span<const unsigned int>    spirv,
    const std::string&               shader_name,
    const char*                      stage_name,
    MTL::Library*&                   out_library
) -> MTL::Function*
{
    if (spirv.empty()) {
        return nullptr;
    }

    spirv_cross::CompilerMSL compiler(spirv.data(), spirv.size());
    spirv_cross::CompilerMSL::Options msl_options;
    msl_options.platform = spirv_cross::CompilerMSL::Options::macOS;
    msl_options.set_msl_version(2, 3);
    msl_options.argument_buffers = true;
    msl_options.argument_buffers_tier = spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
    msl_options.force_active_argument_buffer_resources = true;
    compiler.set_msl_options(msl_options);

    // Keep descriptor set 0 (UBOs/SSBOs) as discrete individual [[buffer(N)]] bindings
    compiler.add_discrete_descriptor_set(0);

    // Move all sampled images to a non-discrete descriptor set for argument buffer.
    // SPIRV-Cross kMaxArgumentBuffers = 8, so set must be 0-7.
    // Set 0 is discrete (UBOs/SSBOs). We use set 7 for textures.
    static constexpr uint32_t texture_descriptor_set = 7;

    // Fixed Metal buffer indices -- UBOs/SSBOs use identity mapping (GLSL binding = Metal index).
    // These higher indices are reserved for the argument buffer and push constant.
    static constexpr uint32_t metal_arg_buffer_index    = 14;
    static constexpr uint32_t metal_push_constant_index = 15;

    spv::ExecutionModel exec_model = compiler.get_execution_model();

    {
        spirv_cross::ShaderResources pre_resources = compiler.get_shader_resources();

        // Move sampled images to texture descriptor set and assign deterministic
        // [[id(N)]] values for fragment/compute stages only.
        // Vertex shaders don't sample textures; their sampled_image declarations
        // (injected via default_uniform_block) are unused. Keep them in the
        // discrete set 0 so they don't create an argument buffer.
        if (exec_model == spv::ExecutionModelFragment || exec_model == spv::ExecutionModelGLCompute) {
            for (const spirv_cross::Resource& resource : pre_resources.sampled_images) {
                compiler.set_decoration(resource.id, spv::DecorationDescriptorSet, texture_descriptor_set);
            }
            std::vector<std::pair<uint32_t, const spirv_cross::Resource*>> sorted_images;
            for (const spirv_cross::Resource& resource : pre_resources.sampled_images) {
                uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                sorted_images.push_back({binding, &resource});
            }
            std::sort(sorted_images.begin(), sorted_images.end());
            uint32_t arg_offset = 0;
            for (const auto& [binding, resource_ptr] : sorted_images) {
                const spirv_cross::SPIRType& type = compiler.get_type(resource_ptr->type_id);
                uint32_t array_size = type.array.empty() ? 1 : type.array[0];
                if (array_size == 0) {
                    array_size = 1; // treat unsized arrays as size 1
                }
                spirv_cross::MSLResourceBinding rb{};
                rb.stage       = exec_model;
                rb.desc_set    = texture_descriptor_set;
                rb.binding     = binding;
                rb.count       = array_size;
                rb.msl_texture = arg_offset;
                rb.msl_sampler = arg_offset + array_size;
                compiler.add_msl_resource_binding(rb);
                arg_offset += 2 * array_size;
            }
        }

        // Set explicit Metal buffer indices for all resources so that
        // GLSL binding N maps to Metal [[buffer(N)]] (identity mapping).
        // This eliminates the need for runtime remapping.
        for (const spirv_cross::Resource& resource : pre_resources.uniform_buffers) {
            uint32_t glsl_binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            spirv_cross::MSLResourceBinding rb{};
            rb.stage      = exec_model;
            rb.desc_set   = 0;
            rb.binding    = glsl_binding;
            rb.msl_buffer = glsl_binding;
            compiler.add_msl_resource_binding(rb);
        }
        for (const spirv_cross::Resource& resource : pre_resources.storage_buffers) {
            uint32_t glsl_binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
            spirv_cross::MSLResourceBinding rb{};
            rb.stage      = exec_model;
            rb.desc_set   = 0;
            rb.binding    = glsl_binding;
            rb.msl_buffer = glsl_binding;
            compiler.add_msl_resource_binding(rb);
        }
        for (const spirv_cross::Resource& resource : pre_resources.push_constant_buffers) {
            static_cast<void>(resource);
            spirv_cross::MSLResourceBinding rb{};
            rb.stage      = exec_model;
            rb.desc_set   = spirv_cross::kPushConstDescSet;
            rb.binding    = spirv_cross::kPushConstBinding;
            rb.msl_buffer = metal_push_constant_index;
            compiler.add_msl_resource_binding(rb);
        }

        // Argument buffer for texture descriptor set -> fixed index 14
        {
            spirv_cross::MSLResourceBinding rb{};
            rb.stage      = exec_model;
            rb.desc_set   = texture_descriptor_set;
            rb.binding    = spirv_cross::kArgumentBufferBinding;
            rb.msl_buffer = metal_arg_buffer_index;
            compiler.add_msl_resource_binding(rb);
        }
    }

    std::string msl;
    try {
        msl = compiler.compile();
    } catch (const spirv_cross::CompilerError& e) {
        std::string error_msg = fmt::format("SPIR-V -> MSL {} compilation failed for '{}': {}", stage_name, shader_name, e.what());
        log_program->error("{}", error_msg);
        device.shader_error(error_msg, "");
        return nullptr;
    }

    if (msl.empty()) {
        return nullptr;
    }

    log_program->info("{} MSL for '{}' compiled successfully ({} bytes):\n{}", stage_name, shader_name, msl.size(), msl);

    // Log resource bindings for debugging
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    for (const spirv_cross::Resource& resource : resources.uniform_buffers) {
        uint32_t glsl_binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t metal_index  = compiler.get_automatic_msl_resource_binding(resource.id);
        log_program->info("  {} UBO '{}': glsl_binding={} -> metal_buffer={}", stage_name, resource.name, glsl_binding, metal_index);
    }
    for (const spirv_cross::Resource& resource : resources.push_constant_buffers) {
        uint32_t metal_index = compiler.get_automatic_msl_resource_binding(resource.id);
        log_program->info("  {} PushConstant '{}': metal_buffer={}", stage_name, resource.name, metal_index);
    }
    for (const spirv_cross::Resource& resource : resources.sampled_images) {
        uint32_t glsl_binding   = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t tex_arg_index  = compiler.get_automatic_msl_resource_binding(resource.id);
        uint32_t smp_arg_index  = compiler.get_automatic_msl_resource_binding_secondary(resource.id);
        log_program->info("  {} Texture '{}': glsl_binding={} -> arg_buffer tex_id={} smp_id={}", stage_name, resource.name, glsl_binding, tex_arg_index, smp_arg_index);
    }
    for (const spirv_cross::Resource& resource : resources.storage_buffers) {
        uint32_t glsl_binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        uint32_t metal_index  = compiler.get_automatic_msl_resource_binding(resource.id);
        log_program->info("  {} SSBO '{}': glsl_binding={} -> metal_buffer={}", stage_name, resource.name, glsl_binding, metal_index);
    }

    NS::Error* error = nullptr;
    NS::String* msl_source = NS::String::alloc()->init(msl.c_str(), NS::UTF8StringEncoding);
    MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();

    out_library = mtl_device->newLibrary(msl_source, options, &error);

    options->release();
    msl_source->release();

    if (out_library == nullptr) {
        const char* error_str = (error != nullptr) ? error->localizedDescription()->utf8String() : "unknown error";
        std::string error_msg = fmt::format("MTL {} library compilation failed for '{}': {}", stage_name, shader_name, error_str);
        log_program->error("{}", error_msg);
        device.shader_error(error_msg, msl);
        return nullptr;
    }

    // SPIRV-Cross generates entry point named "main0"
    NS::String* entry_name = NS::String::string("main0", NS::UTF8StringEncoding);
    MTL::Function* function = out_library->newFunction(entry_name);

    if (function == nullptr) {
        NS::Array* function_names = out_library->functionNames();
        std::string available;
        for (NS::UInteger i = 0; i < function_names->count(); ++i) {
            NS::String* fn_name = static_cast<NS::String*>(function_names->object(i));
            if (i > 0) available += ", ";
            available += fn_name->utf8String();
        }
        std::string error_msg = fmt::format("{} function 'main0' not found in '{}'. Available: {}", stage_name, shader_name, available);
        log_program->error("{}", error_msg);
        device.shader_error(error_msg, "");
    }

    return function;
}

} // anonymous namespace

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, Shader_stages_create_info&& create_info)
    : m_device               {device}
    , m_create_info           {std::move(create_info)}
    , m_is_valid              {true}
    , m_glslang_shader_stages{*this}
{
    compile_shaders();
    link_program();
}

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, const Shader_stages_create_info& create_info)
    : m_device               {device}
    , m_create_info           {create_info}
    , m_is_valid              {true}
    , m_glslang_shader_stages{*this}
{
    compile_shaders();
    link_program();
}

Shader_stages_prototype_impl::~Shader_stages_prototype_impl() noexcept
{
    if (m_vertex_function != nullptr) {
        m_vertex_function->release();
    }
    if (m_fragment_function != nullptr) {
        m_fragment_function->release();
    }
    if (m_vertex_library != nullptr) {
        m_vertex_library->release();
    }
    if (m_fragment_library != nullptr) {
        m_fragment_library->release();
    }
    if (m_compute_function != nullptr) {
        m_compute_function->release();
    }
    if (m_compute_library != nullptr) {
        m_compute_library->release();
    }
}

auto Shader_stages_prototype_impl::name() const -> const std::string& { return m_create_info.name; }
auto Shader_stages_prototype_impl::create_info() const -> const Shader_stages_create_info& { return m_create_info; }
auto Shader_stages_prototype_impl::is_valid() -> bool { return m_is_valid; }

void Shader_stages_prototype_impl::compile_shaders()
{
    for (const Shader_stage& shader : m_create_info.shaders) {
        if (shader.type == Shader_type::geometry_shader) {
            log_program->warn("Metal does not support geometry shaders, marking invalid: {}", m_create_info.name);
            m_is_valid = false;
            return;
        }
        const bool compile_ok = m_glslang_shader_stages.compile_shader(m_device, shader);
        if (!compile_ok) {
            std::string error_msg = fmt::format("GLSL compilation failed for shader: {}", m_create_info.name);
            log_program->error("{}", error_msg);
            std::string source = m_create_info.final_source(m_device, shader, nullptr);
            m_device.shader_error(error_msg, source);
            m_is_valid = false;
            return;
        }
    }
}

auto Shader_stages_prototype_impl::link_program() -> bool
{
    if (!m_is_valid) {
        return false;
    }

    const bool link_ok = m_glslang_shader_stages.link_program();
    if (!link_ok) {
        log_program->error("GLSL -> SPIR-V link failed for: {}", m_create_info.name);
        m_is_valid = false;
        return false;
    }

    Device_impl& device_impl = m_device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    if (mtl_device == nullptr) {
        m_is_valid = false;
        return false;
    }

    // Compile each stage into its own MTL::Library
    m_vertex_function = compile_spirv_to_mtl_function(
        m_device,
        mtl_device,
        m_glslang_shader_stages.get_spirv_binary(Shader_type::vertex_shader),
        m_create_info.name,
        "Vertex",
        m_vertex_library
    );

    m_fragment_function = compile_spirv_to_mtl_function(
        m_device,
        mtl_device,
        m_glslang_shader_stages.get_spirv_binary(Shader_type::fragment_shader),
        m_create_info.name,
        "Fragment",
        m_fragment_library
    );

    m_compute_function = compile_spirv_to_mtl_function(
        m_device,
        mtl_device,
        m_glslang_shader_stages.get_spirv_binary(Shader_type::compute_shader),
        m_create_info.name,
        "Compute",
        m_compute_library
    );

    if ((m_vertex_function == nullptr) && (m_compute_function == nullptr)) {
        log_program->error("No vertex function for: {}", m_create_info.name);
        m_is_valid = false;
        return false;
    }

    log_program->info(
        "Metal shader '{}': vertex={}, fragment={}",
        m_create_info.name,
        (m_vertex_function   != nullptr) ? "ok" : "missing",
        (m_fragment_function != nullptr) ? "ok" : "missing"
    );

    return true;
}

void Shader_stages_prototype_impl::dump_reflection() const {}

auto Shader_stages_prototype_impl::get_final_source(
    const Shader_stage&         shader,
    std::optional<unsigned int> gl_name
) -> std::string
{
    return m_create_info.final_source(m_device, shader, nullptr, gl_name);
}

auto Shader_stages_prototype_impl::get_vertex_function() const -> MTL::Function*
{
    return m_vertex_function;
}

auto Shader_stages_prototype_impl::get_fragment_function() const -> MTL::Function*
{
    return m_fragment_function;
}

auto Shader_stages_prototype_impl::get_compute_function() const -> MTL::Function*
{
    return m_compute_function;
}


} // namespace erhe::graphics
