#include "erhe_graphics/fragment_outputs.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_gl/command_info.hpp"
# include "erhe_gl/enum_string_functions.hpp"
# include "erhe_gl/wrapper_enums.hpp"
#endif

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/glsl_file_loader.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <sstream>
#include <string>

namespace erhe::graphics {

auto glsl_token(const Glsl_type attribute_type) -> const char*
{
    switch (attribute_type) {
        //using enum gl::Attribute_type;
        case Glsl_type::float_:            return "float    ";
        case Glsl_type::float_vec2:        return "vec2     ";
        case Glsl_type::float_vec3:        return "vec3     ";
        case Glsl_type::float_vec4:        return "vec4     ";
        case Glsl_type::bool_:             return "bool     ";
        case Glsl_type::int_:              return "int      ";
        case Glsl_type::int_vec2:          return "ivec2    ";
        case Glsl_type::int_vec3:          return "ivec3    ";
        case Glsl_type::int_vec4:          return "ivec4    ";
        case Glsl_type::unsigned_int:      return "uint     ";
        case Glsl_type::unsigned_int_vec2: return "uvec2    ";
        case Glsl_type::unsigned_int_vec3: return "uvec3    ";
        case Glsl_type::unsigned_int_vec4: return "uvec4    ";
        case Glsl_type::unsigned_int64:    return "uint64_t ";
        case Glsl_type::float_mat_2x2:     return "mat2     ";
        case Glsl_type::float_mat_3x3:     return "mat3     ";
        case Glsl_type::float_mat_4x4:     return "mat4     ";
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

auto Shader_stages_create_info::attributes_source() const -> std::string
{
    std::stringstream sb;

    // Apply attrib location bindings
    if (vertex_format != nullptr) {
        Vertex_input_state_data vertex_input = Vertex_input_state_data::make(*vertex_format);
        sb << "// Attributes\n";
        for (const auto& attribute : vertex_input.attributes) {
            sb << "layout(location = " << attribute.layout_location << ") in ";
            sb << glsl_token(to_glsl_attribute_type(attribute.format)) << " ";
            sb << attribute.name,
            sb << ";\n";
        }
        for (const auto& attribute : vertex_input.attributes) {
            sb << "#define ERHE_ATTRIBUTE_" << attribute.name << " 1\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::attribute_defines_source() const -> std::string
{
    // Just the ERHE_ATTRIBUTE_<name> defines -- no input declarations.
    // Fragment shaders (and any other non-vertex stage) need these defines so
    // #ifdef ERHE_ATTRIBUTE_* branches read the interpolated varyings that
    // were actually declared on the vertex side.
    std::stringstream sb;

    if (vertex_format != nullptr) {
        Vertex_input_state_data vertex_input = Vertex_input_state_data::make(*vertex_format);
        sb << "// Attribute defines\n";
        for (const auto& attribute : vertex_input.attributes) {
            sb << "#define ERHE_ATTRIBUTE_" << attribute.name << " 1\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::fragment_outputs_source() const -> std::string
{
    std::stringstream sb;

    sb << "// Fragment outputs\n";
    if (fragment_outputs != nullptr) {
        sb << fragment_outputs->source();
    }
    sb << "\n";

    return sb.str();
}

auto Shader_stages_create_info::struct_types_source() const -> std::string
{
    std::stringstream sb;

    if (struct_types.size() > 0) {
        sb << "// Struct types\n";
        for (const auto& struct_type : struct_types) {
            ERHE_VERIFY(struct_type != nullptr);
            sb << struct_type->get_source();
            sb << "\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::interface_blocks_source() const -> std::string
{
    std::stringstream sb;

    if (interface_blocks.size() > 0) {
        sb << "// Blocks\n";
        for (const auto* block : interface_blocks) {
            sb << block->get_source();
            sb << "\n";
        }
        sb << "\n";
    }

    return sb.str();
}

auto Shader_stages_create_info::interface_source() const -> std::string
{
    std::stringstream sb;
    sb << attributes_source      ();
    sb << fragment_outputs_source();
    sb << struct_types_source    ();
    sb << interface_blocks_source();
    return sb.str();
}

auto Shader_stages_create_info::final_source(
    Device&                             graphics_device,
    const Shader_stage&                 shader,
    std::vector<std::filesystem::path>* paths,
    std::optional<unsigned int>         gl_name
) const -> std::string
{
    ERHE_PROFILE_FUNCTION();

    std::stringstream sb;
    sb << "#version " << graphics_device.get_info().glsl_version << " core\n\n";
    sb << "// " << c_str(shader.type);
    if (gl_name.has_value()) {
        sb << " " << gl_name.value();
    }
    bool first_path = true;
    for (const std::filesystem::path& path : shader.paths) {
        if (path.empty()) {
            continue;
        }
        if (!first_path) {
            sb << ",";
        }
        sb << " " << path;
        first_path = false;
    }
    sb << "\n";

    if (!pragmas.empty()) {
        sb << "// Pragmas\n";
        for (const auto& i : pragmas) {
            sb << "#pragma " << i << "\n";
        }
        sb << "\n";
    }

    if (!extensions.empty()) {
        sb << "// Extensions\n";
        for (const auto& i : extensions) {
            if (i.shader_stage == shader.type) {
                sb << "#extension " << i.extension << " : require\n";
            }
        }
        sb << "\n";
    }

    // Multiview prelude.
    //
    // GL_EXT_multiview is the Vulkan / SPIR-V flavour of multiview:
    // the view count is taken from the render pass (subpass.viewMask
    // / VkRenderPassMultiviewCreateInfo), not from a shader layout
    // qualifier. The shader source only needs the extension and uses
    // `gl_ViewIndex` to read the current view. The OpenGL flavour
    // (GL_OVR_multiview2) does require `layout(num_views = N) in;`
    // in the vertex shader, but that path is out of scope; this
    // engine targets Vulkan multiview only on Quest.
    sb << "#define ERHE_VIEW_COUNT " << view_count << "\n";
    if (view_count > 1) {
        sb << "// Multiview\n";
        sb << "#extension GL_EXT_multiview : require\n";
        sb << "#define ERHE_MULTIVIEW 1\n";
        sb << "\n";
    }

    sb << "#define ERHE_GLSL_VERSION " << graphics_device.get_info().glsl_version << "\n";

    // Driver-workaround defines. We deliberately do NOT expose the graphics API
    // (Vulkan/Metal/GL) to shaders; shaders branch on the specific capability or
    // workaround they need, not on the backend. Each WORKAROUND_* macro is
    // emitted only on devices detected to need it (see Device_info and the
    // backend device init), so it becomes part of the shader variant. Policy:
    // doc/shader_workarounds.md.
    if (graphics_device.get_info().workaround_no_compute_storage_image_read) {
        sb << "#define WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ 1\n";
    }

    // Gate shader code that uses gl_ClipDistance / user clip planes. On
    // Vulkan this corresponds to the shaderClipDistance device feature
    // (e.g. Mali-G715 does not expose it). On GL this is universally
    // available at the versions erhe targets, but the gate is still
    // emitted so shader sources have a single conditional.
    if (graphics_device.get_info().use_clip_distance) {
        sb << "#define ERHE_HAS_CLIP_DISTANCE 1\n";
    }

#if defined(ERHE_GRAPHICS_API_OPENGL)
    if (graphics_device.get_info().use_shader_storage_buffers && (graphics_device.get_info().gl_version < 430)) {
        ERHE_VERIFY(gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shader_storage_buffer_object));
        sb << "#extension GL_ARB_shader_storage_buffer_object : enable\n";
        sb << "#define ERHE_HAS_ARB_SHADER_STORAGE_BUFFER_OBJECT 1\n";
    }
    if (graphics_device.get_info().texture_heap_path == Texture_heap_path::opengl_bindless_textures) {
        sb << "#extension GL_ARB_bindless_texture : enable\n";
        sb << "#define ERHE_TEXTURE_HEAP_OPENGL_BINDLESS 1\n";
    }
    if (graphics_device.get_info().texture_heap_path == Texture_heap_path::opengl_sampler_array) {
        sb << "#define ERHE_TEXTURE_HEAP_OPENGL_SAMPLER_ARRAY 1\n";
    }
    if (gl::is_extension_supported(gl::Extension::Extension_GL_ARB_shading_language_packing)) {
        sb << "#extension GL_ARB_shading_language_packing : enable\n";
        sb << "#define ERHE_HAS_ARB_SHADING_LANGUAGE_PACKING 1\n";
    } else {
        // Provide polyfill implementations for pack/unpack functions
        // when GL_ARB_shading_language_packing is not available
        // (e.g. macOS OpenGL 4.1 where these are missing despite being GLSL 4.00 core)
        sb << "vec2 unpackSnorm2x16(uint p) {\n"
              "    uvec2 u = uvec2(p & 0xFFFFu, (p >> 16) & 0xFFFFu);\n"
              "    ivec2 s = ivec2(u);\n"
              "    if (s.x >= 0x8000) s.x -= 0x10000;\n"
              "    if (s.y >= 0x8000) s.y -= 0x10000;\n"
              "    return clamp(vec2(s) / 32767.0, -1.0, 1.0);\n"
              "}\n"
              "vec2 unpackUnorm2x16(uint p) {\n"
              "    return vec2(\n"
              "        float(p & 0xFFFFu) / 65535.0,\n"
              "        float((p >> 16) & 0xFFFFu) / 65535.0\n"
              "    );\n"
              "}\n"
              "vec4 unpackUnorm4x8(uint p) {\n"
              "    return vec4(\n"
              "        float(p & 0xFFu) / 255.0,\n"
              "        float((p >> 8) & 0xFFu) / 255.0,\n"
              "        float((p >> 16) & 0xFFu) / 255.0,\n"
              "        float((p >> 24) & 0xFFu) / 255.0\n"
              "    );\n"
              "}\n";
    }
    if (graphics_device.get_info().use_multi_draw_indirect_core) {
        sb << "#define ERHE_DRAW_ID gl_DrawID\n";
    } else if (graphics_device.get_info().use_multi_draw_indirect_arb) {
        sb << "#extension GL_ARB_shader_draw_parameters : enable\n";
        sb << "#define ERHE_HAS_ARB_SHADER_DRAW_PARAMETERS 1\n";
        sb << "#define ERHE_DRAW_ID gl_DrawIDARB\n";
    } else if (graphics_device.get_info().emulate_multi_draw_indirect) {
        sb << "uniform int ERHE_DRAW_ID;\n";
    } else {
        log_glsl->warn("gl_DrawID is not supported");
    }
    sb << "\n";
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
    if (graphics_device.get_info().use_multi_draw_indirect_core) {
        sb << "#define ERHE_DRAW_ID gl_DrawID\n";
    } else {
        // Emulate multi-draw indirect with per-draw push constants.
        // Only emit for vertex/compute -- fragment shaders receive draw ID via interpolated varying.
        if (shader.type == Shader_type::vertex_shader || shader.type == Shader_type::compute_shader) {
            sb << "layout(push_constant) uniform Erhe_draw_id_block { int ERHE_DRAW_ID; };\n";
        }
    }
    // Only declare the bindless texture heap when the bind group layout
    // opts in. Shaders that don't sample from the heap (e.g. most compute
    // shaders) must NOT declare set 1, or validation warns that the
    // declared descriptor set is never bound.
    if ((bind_group_layout == nullptr) || bind_group_layout->uses_texture_heap()) {
        sb << "#define ERHE_TEXTURE_HEAP_VULKAN_DESCRIPTOR_INDEXING 1\n";
        sb << "#extension GL_EXT_nonuniform_qualifier : enable\n";
        sb << "layout(set = 1, binding = 0) uniform sampler2D erhe_texture_heap[];\n";
    }
    // Vulkan SPIR-V uses gl_VertexIndex / gl_InstanceIndex instead of gl_VertexID / gl_InstanceID
    sb << "#define gl_VertexID gl_VertexIndex\n";
    sb << "#define gl_InstanceID gl_InstanceIndex\n";

    // Optional shader-arithmetic / storage features. Each block mirrors a
    // VkPhysicalDevice feature flag we enabled at device creation. We emit
    // the corresponding GLSL extension directive with ": require" so shader
    // source can use the types directly without its own #extension line,
    // and an ERHE_HAS_* define so #ifdef-guards still let shaders pick code
    // paths based on which features are actually live. With ": require",
    // mismatch between the device feature flag and glslang language
    // support fails loudly at the #extension directive instead of silently
    // at first use.
    const Device_info& vulkan_device_info = graphics_device.get_info();
    if (vulkan_device_info.shader_float16) {
        sb << "#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require\n";
        sb << "#define ERHE_HAS_SHADER_FLOAT16 1\n";
    }
    if (vulkan_device_info.shader_int8) {
        sb << "#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require\n";
        sb << "#define ERHE_HAS_SHADER_INT8 1\n";
    }
    // GL_EXT_shader_16bit_storage covers all four 16-bit storage classes;
    // emit the directive once if any of the four feature bits is enabled.
    // The SPIR-V backend only emits the specific Storage*16* capability
    // for storage classes the shader actually references, so the unused
    // ones cost nothing.
    if (vulkan_device_info.storage_buffer_16bit_access ||
        vulkan_device_info.uniform_and_storage_buffer_16bit_access ||
        vulkan_device_info.storage_push_constant_16 ||
        vulkan_device_info.storage_input_output_16) {
        sb << "#extension GL_EXT_shader_16bit_storage : require\n";
    }
    if (vulkan_device_info.storage_buffer_16bit_access) {
        sb << "#define ERHE_HAS_STORAGE_BUFFER_16BIT_ACCESS 1\n";
    }
    if (vulkan_device_info.uniform_and_storage_buffer_16bit_access) {
        sb << "#define ERHE_HAS_UNIFORM_AND_STORAGE_BUFFER_16BIT_ACCESS 1\n";
    }
    if (vulkan_device_info.storage_push_constant_16) {
        sb << "#define ERHE_HAS_STORAGE_PUSH_CONSTANT_16 1\n";
    }
    if (vulkan_device_info.storage_input_output_16) {
        sb << "#define ERHE_HAS_STORAGE_INPUT_OUTPUT_16 1\n";
    }
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
    // Metal does not support multi-draw indirect / gl_DrawID; emulate with push constant.
    // Only emit for vertex/compute -- fragment shaders receive draw ID via interpolated varying.
    if (shader.type == Shader_type::vertex_shader || shader.type == Shader_type::compute_shader) {
        sb << "layout(push_constant) uniform Erhe_draw_id_block { int ERHE_DRAW_ID; };\n";
    }
    sb << "#define ERHE_BACKEND_METAL 1\n";
    // ERHE_TEXTURE_HEAP_METAL_ARGUMENT_BUFFER is the Metal-specific shader code
    // path for the texture heap. It is intentionally kept distinct from the
    // OpenGL/Vulkan defines so that the Metal branch in cross-backend GLSL
    // helpers (e.g. erhe_texture.glsl) can diverge in the future. Today its
    // body samples from the same `s_texture[]` sampler array that the
    // default uniform block declares -- which is what the Metal
    // Texture_heap_impl actually fills via its argument encoder. We do NOT
    // declare a separate `erhe_texture_heap[]` here: there is no host code
    // backing it on Metal, and a duplicate (set=1, binding=0) sampler image
    // collides with the default-uniform-block samplers under SPIRV-Cross.
    sb << "#define ERHE_TEXTURE_HEAP_METAL_ARGUMENT_BUFFER 1\n";
    // Vulkan SPIR-V uses gl_VertexIndex / gl_InstanceIndex instead of gl_VertexID / gl_InstanceID
    sb << "#define gl_VertexID gl_VertexIndex\n";
    sb << "#define gl_InstanceID gl_InstanceIndex\n";
#endif

    if (shader.type == Shader_type::vertex_shader) {
        sb << attributes_source();
    } else {
        // Non-vertex stages still need ERHE_ATTRIBUTE_<name> defines so
        // their #ifdef guards agree with the vertex declarations and read
        // the interpolated varyings the vertex shader actually wrote.
        sb << attribute_defines_source();
        if (shader.type == Shader_type::fragment_shader) {
            sb << fragment_outputs_source();
        }
    }

    if (defines.size() > 0) {
        sb << "// Defines\n";
        for (const auto& i : defines) {
            // Comment-only entry: empty name means "do not emit #define,
            // just emit the value as a // comment line in the preamble".
            // Used by the standard shader variant cache to surface the
            // disabled boolean axes alongside the enabled ones so RenderDoc
            // captures show the full key in the preamble.
            if (i.first.empty()) {
                sb << "// " << i.second << '\n';
            } else {
                sb << "#define " << i.first << " " << i.second << '\n';
            }
        }
        sb << "\n";
    }

    sb << struct_types_source();
    sb << interface_blocks_source();

    // Default uniform block: the sampler declarations are owned by the
    // bind_group_layout and synthesized from its create_info bindings.
    // The shader source emitter pulls them out here and injects them
    // into the GLSL preamble as uniform sampler2D / sampler2DArrayShadow /
    // ... declarations.
    if (bind_group_layout != nullptr) {
        const Shader_resource& default_uniform_block = bind_group_layout->get_default_uniform_block();
        if (!default_uniform_block.get_members().empty()) {
            const uint32_t sampler_offset = bind_group_layout->get_sampler_binding_offset();
            if (sampler_offset > 0) {
                sb << "#define ERHE_SAMPLER_BINDING_OFFSET " << sampler_offset << "\n";
            }
            sb << "// Default uniform block\n";
            sb << default_uniform_block.get_source(0, sampler_offset);
            sb << "\n";
        }
    }

    if (!shader.source.empty()) {
        sb << shader.source;
        if (shader.source.back() != '\n') {
            sb << '\n';
        }
    }
    if (!shader.paths.empty()) {
        Glsl_file_loader loader{graphics_device};
        ERHE_VERIFY(shader.paths.size() == 1);
        std::string source = loader.read_shader_source_file(shader.paths.front(), extra_include_paths);
        if (paths != nullptr) {
            for (const std::filesystem::path& path : loader.get_file_paths()) {
                paths->push_back(path);
            }
        }
        // If the loader could not read the source file it returns "".
        // Don't append the engine preamble alone -- propagate empty so
        // Shader_stages_prototype_impl::compile sees source.empty() and
        // short-circuits to state_fail before glCompileShader runs and
        // produces a misleading "No definition of main" linker error.
        if (source.empty()) {
            return std::string{};
        }
        sb << source;
    }

    return sb.str();
}

void Shader_stages_create_info::add_interface_block(const Shader_resource* interface_block)
{
    ERHE_VERIFY(interface_block != nullptr);
    interface_blocks.push_back(interface_block);
}

auto Shader_stages_create_info::get_description() const -> std::string
{
    std::stringstream ss;
    ss << name;
    for (const Shader_stage& shader_stage : shaders) {
        ss << " stage: " << shader_stage.get_description();
    }
    return ss.str();
}

} // namespace erhe::graphics
