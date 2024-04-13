#include "erhe_renderer/text_renderer.hpp"
#include "erhe_renderer/renderer_log.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_graphics/shader_monitor.hpp"

#include "erhe_graphics/scoped_buffer_mapping.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"
#include "erhe_ui/font.hpp"

namespace erhe::renderer
{

using glm::mat4;
using glm::vec3;
using glm::vec4;


Text_renderer::Frame_resources::Frame_resources(
    igl::IDevice&                              device,
    igl::RenderPipelineDesc::TargetDesc&       target,
    const std::size_t                          vertex_count,
    const std::shared_ptr<igl::IShaderStages>& shader_stages,
    erhe::graphics::Vertex_attribute_mappings& attribute_mappings,
    erhe::graphics::Vertex_format&             vertex_format,
    const std::size_t                          slot
)
    : vertex_buffer{
        device.createBuffer(
            igl::BufferDesc(
                static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Vertex),
                nullptr,
                vertex_count * vertex_format.stride(),
                igl::ResourceStorage::Shared,
                0,
                fmt::format("Text Renderer Vertex Buffer {}", slot)
            ),
            nullptr
        )
    }
    , projection_buffer{
        device.createBuffer(
            igl::BufferDesc(
                static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Uniform),
                nullptr,
                1024, // TODO
                igl::ResourceStorage::Shared,
                0,
                fmt::format("Text Renderer Projection Buffer {}", slot)
            ),
            nullptr
        )
    }
    , vertex_input{
        vertex_format.make_vertex_input_state(device, attribute_mappings, vertex_buffer.get())
    }
    , pipeline{
        device.createRenderPipeline(
            igl::RenderPipelineDesc{
                .vertexInputState   = vertex_input,
                .shaderStages       = shader_stages,
                .targetDesc         = target,
                .cullMode           = igl::CullMode::Disabled,
                .frontFaceWinding   = igl::WindingMode::CounterClockwise,
                .polygonFillMode    = igl::PolygonFillMode::Fill,
                .sampleCount        = 1,
                .debugName          = igl::genNameHandle("Text renderer")
            },
            nullptr
        )
    }
{
}

static constexpr std::string_view c_text_renderer_initialize_component{"Text_renderer::initialize_component()"};

Text_renderer::Text_renderer(igl::IDevice& device)
    : m_device{device}
   ,  m_projection_block{
        device,
        "projection",
        0,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , m_clip_from_window_resource{m_projection_block.add_mat4 ("clip_from_window")}
    , m_texture_resource         {m_projection_block.add_uvec2("texture")}
    , m_u_clip_from_window_size  {m_clip_from_window_resource->size_bytes()}
    , m_u_clip_from_window_offset{m_clip_from_window_resource->offset_in_parent()}
    , m_u_texture_size           {m_texture_resource->size_bytes()}
    , m_u_texture_offset         {m_texture_resource->offset_in_parent()}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , m_attribute_mappings{
        device,
        { // initializer list
            erhe::graphics::Vertex_attribute_mapping::a_position_float_vec3(),
            erhe::graphics::Vertex_attribute_mapping::a_color_float_vec4(),
            erhe::graphics::Vertex_attribute_mapping::a_texcoord_float_vec2()
        }
    }
    , m_vertex_format{
        erhe::graphics::Vertex_attribute::position_float3(),
        erhe::graphics::Vertex_attribute::color_ubyte4(),
        erhe::graphics::Vertex_attribute::texcoord0_float2()
    }
    , m_index_buffer{
        device.createBuffer(
            igl::BufferDesc(
                static_cast<igl::BufferDesc::BufferType>(igl::BufferDesc::BufferTypeBits::Index),
                nullptr,
                index_stride * index_count,
                igl::ResourceStorage::Private,
                0,
                "Text renderer index buffer"
            ),
            nullptr
        )
    }
    , m_nearest_sampler{
        m_device.createSamplerState(
            igl::SamplerStateDesc{
                .minFilter            = igl::SamplerMinMagFilter::Nearest,
                .magFilter            = igl::SamplerMinMagFilter::Nearest,
                .mipFilter            = igl::SamplerMipFilter::Disabled,
                .addressModeU         = igl::SamplerAddressMode::Clamp,
                .addressModeV         = igl::SamplerAddressMode::Clamp,
                .addressModeW         = igl::SamplerAddressMode::Repeat,
                .depthCompareFunction = igl::CompareFunction::LessEqual,
                .mipLodMin            = 0,
                .mipLodMax            = 15,
                .maxAnisotropic       = 16,
                .depthCompareEnabled  = false,
                .debugName            = "Text Renderer Sampler"
            },
            nullptr
        )
    }
    , m_vertex_writer    {device}
    , m_projection_writer{device}
{
    ERHE_PROFILE_FUNCTION();

    auto ini = erhe::configuration::get_ini("erhe.ini", "text_renderer");
    ini->get("enabled",   config.enabled);
    ini->get("font_size", config.font_size);

    if (!config.enabled) {
        log_startup->info("Text renderer disabled due to erhe.ini setting");
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_initialize_component};

    std::vector<uint16_t> gpu_index_data(max_quad_count * 5);
    std::size_t offset      {0};
    uint16_t    vertex_index{0};
    for (unsigned int i = 0; i < max_quad_count; ++i) {
        gpu_index_data[offset + 0] = vertex_index;
        gpu_index_data[offset + 1] = vertex_index + 1;
        gpu_index_data[offset + 2] = vertex_index + 2;
        gpu_index_data[offset + 3] = vertex_index + 3;
        gpu_index_data[offset + 4] = uint16_primitive_restart;
        vertex_index += 4;
        offset += 5;
    }

    m_index_buffer->upload(
        &gpu_index_data[0], 
        igl::BufferRange(sizeof(uint16_t) * max_quad_count * 5, 0)
    );

    m_font = std::make_unique<erhe::ui::Font>(
        device,
        "res/fonts/SourceSansPro-Regular.otf",
        config.font_size,
        0.0f // TODO reimplement outline better 1.0f
    );

    {
        ERHE_PROFILE_SCOPE("shader");

        const auto shader_path = std::filesystem::path("res") / std::filesystem::path("shaders");
        const std::filesystem::path vs_path = shader_path / std::filesystem::path("text.vert");
        const std::filesystem::path fs_path = shader_path / std::filesystem::path("text.frag");
        erhe::graphics::Shader_stages_create_info create_info{
            .name                      = "text",
            .interface_blocks          = { &m_projection_block },
            .vertex_attribute_mappings = &m_attribute_mappings,
            .fragment_outputs          = &m_fragment_outputs,
            .shaders = {
                { igl::ShaderStage::Vertex,   vs_path },
                { igl::ShaderStage::Fragment, fs_path }
            }
        };

        create_info.samplers.add_sampler(
            "s_texture",
            erhe::graphics::Glsl_type::sampler_2d,
            0
        );

        //if (graphics_instance.info.use_bindless_texture) {
        //    create_info.extensions.push_back({igl::ShaderStage::fragment_shader, "GL_ARB_bindless_texture"});
        //    create_info.defines.emplace_back("ERHE_BINDLESS_TEXTURE", "1");
        //} else 
        //{
        //    m_default_uniform_block.add_sampler(
        //        "s_texture",
        //        igl::UniformType::sampler_2d,
        //        0
        //    );
        //    create_info.default_uniform_block = &m_default_uniform_block;
        //}

        erhe::graphics::Shader_stages_prototype prototype{device, create_info};
        if (!prototype.is_valid()) {
            log_startup->error("Text renderer shader compilation failed");
            config.enabled = false;
            m_font.reset();
            return;
        }

        m_shader_stages = std::make_unique<erhe::graphics::Shader_stages>(std::move(prototype));
        //// graphics_instance.shader_monitor.add(create_info, m_shader_stages.get());
    }

    create_frame_resources();
}

void Text_renderer::create_frame_resources()
{
    ERHE_PROFILE_FUNCTION();

    constexpr std::size_t vertex_count{65536 * 8};
    for (std::size_t slot = 0; slot < s_frame_resources_count; ++slot) {
        m_frame_resources.emplace_back(
            m_device,
            vertex_count,
            m_shader_stages.get(),
            m_attribute_mappings,
            m_vertex_format,
            slot
        );
    }
}

auto Text_renderer::current_frame_resources() -> Text_renderer::Frame_resources&
{
    return m_frame_resources[m_current_frame_resource_slot];
}

void Text_renderer::next_frame()
{
    m_current_frame_resource_slot = (m_current_frame_resource_slot + 1) % s_frame_resources_count;
    m_vertex_writer    .reset();
    m_projection_writer.reset();
    m_index_range_first = 0;
    m_index_count       = 0;
}

void Text_renderer::print(
    const glm::vec3        text_position,
    const uint32_t         text_color,
    const std::string_view text
)
{
    ERHE_PROFILE_FUNCTION();

    if (!config.enabled || !m_font) {
        return;
    }

    const igl::IBuffer*       vertex_buffer     = current_frame_resources().vertex_buffer.get();
    const std::size_t         quad_count        = m_font->get_glyph_count(text);
    const std::size_t         vertex_byte_count = quad_count * 4 * m_vertex_format.stride();
    const auto                vertex_gpu_data   = m_vertex_writer.begin(vertex_buffer, vertex_byte_count);
    std::byte* const          start             = vertex_gpu_data.data();
    const std::size_t         byte_count        = vertex_gpu_data.size_bytes();
    const std::size_t         word_count        = byte_count / sizeof(float);
    const std::span<float>    gpu_float_data{reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t> gpu_uint_data {reinterpret_cast<uint32_t*>(start), word_count};

    erhe::ui::Rectangle bounding_box;
    const vec3          snapped_position{
        std::floor(text_position.x + 0.5f),
        std::floor(text_position.y + 0.5f),
        text_position.z
    };
    const std::size_t quad_count2 = m_font->print(
        gpu_float_data,
        gpu_uint_data,
        text,
        snapped_position,
        text_color,
        bounding_box
    );
    ERHE_VERIFY(quad_count2 == quad_count);
    m_vertex_writer.write_offset += vertex_byte_count; // quad_count * 4 * m_vertex_format.stride();
    m_vertex_writer.end();
    m_index_count += quad_count * 5;
}

auto Text_renderer::font_size() -> float
{
    return static_cast<float>(config.font_size);
}

auto Text_renderer::measure(const std::string_view text) const -> erhe::ui::Rectangle
{
    return m_font
        ? m_font->measure(text)
        : erhe::ui::Rectangle{};
}

static constexpr std::string_view c_text_renderer_render{"Text_renderer::render()"};

void Text_renderer::render(
    igl::IRenderCommandEncoder& renderEncoder,
    erhe::math::Viewport        viewport
)
{
    if (m_index_count == 0) {
        return;
    }

    ERHE_PROFILE_FUNCTION();

    //ERHE_PROFILE_GPU_SCOPE(c_text_renderer_render)

    //// const auto handle = m_graphics_instance.get_handle(
    ////     *m_font->texture().get(),
    ////     m_nearest_sampler
    //// );

    erhe::graphics::Scoped_debug_group pass_scope{c_text_renderer_render};

    const auto&               projection_buffer_shared = current_frame_resources().projection_buffer;
    const igl::IBuffer*       projection_buffer        = projection_buffer_shared.get();
    const auto                projection_gpu_data      = m_projection_writer.begin(projection_buffer, m_projection_block.size_bytes());
    std::byte* const          start                    = projection_gpu_data.data();
    const std::size_t         byte_count               = projection_gpu_data.size_bytes();
    const std::size_t         word_count               = byte_count / sizeof(float);
    const std::span<float>    gpu_float_data {reinterpret_cast<float*   >(start), word_count};
    const std::span<uint32_t> gpu_uint32_data{reinterpret_cast<uint32_t*>(start), word_count};

    const mat4 clip_from_window = erhe::math::create_orthographic(
        static_cast<float>(viewport.x), static_cast<float>(viewport.width),
        static_cast<float>(viewport.y), static_cast<float>(viewport.height),
        0.0f,
        1.0f
    );
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    write(gpu_float_data, m_u_clip_from_window_offset, as_span(clip_from_window));
    m_projection_writer.write_offset += m_u_clip_from_window_size;

    m_projection_writer.end();

    const auto& frame_resources = current_frame_resources();
    const auto& pipeline = frame_resources.pipeline;
    renderEncoder.bindRenderPipelineState(pipeline);
    renderEncoder.bindBuffer(
        m_projection_block.binding_point(),
        igl::BindTarget::kAllGraphics,
        projection_buffer_shared,
        m_projection_writer.range.first_byte_offset
        // , m_projection_writer.range.byte_count
    );

    renderEncoder.bindSamplerState(0, igl::BindTarget::kFragment, m_nearest_sampler.get());
    renderEncoder.bindTexture(0, igl::BindTarget::kFragment, m_font->texture());

    renderEncoder.drawIndexed(
        igl::PrimitiveType::TriangleStrip,
        m_index_count,
        igl::IndexFormat::UInt16,
        *m_index_buffer.get(),
        0
    );

    m_index_range_first += m_index_count;
    m_index_count = 0;
}

} // namespace erhe::renderer
