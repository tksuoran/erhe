#include "scene/draw_mode_cards.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/draw_mode.hpp"
#include "scene/scene_root.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_file/file.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_gltf/image_transfer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/image_loader.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/draw_mode_description.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace editor {

using erhe::scene::Draw_mode_card_face;
using erhe::scene::Draw_mode_card_geometry;
using erhe::scene::Draw_mode_card_visibility;
using erhe::scene::c_draw_mode_card_face_count;

namespace {

// The one metadata key `fromTexture` places a card from
// (UsdImagingDrawModeAdapter::_GetMatrixFromImageMetadata): a world-to-screen
// 4x4 matrix, row major.
constexpr std::string_view c_worldtoscreen_key{"worldtoscreen"};

// The gap `cross` puts between the two coplanar cards of one axis, so that
// the front card of the pair wins the depth test everywhere. The imaging
// adapter uses exactly this value.
constexpr float c_cross_epsilon = 0x1.0p-23f;

// One card face: its four corners in the prim's own space, in the order the
// imaging adapter emits them, so the quad's winding faces outward.
class Card_quad
{
public:
    std::array<glm::vec3, 4> corner{};
    std::array<glm::vec2, 4> uv    {};
};

// The imaging adapter's unflipped UV quad
// (UsdImagingDrawModeAdapter::_GetUVsForQuad(false, false)) in USD's `st`
// space, converted to erhe's texture coordinates by the one involution
// `v' = 1 - v` (src/erhe/usd/notes.md, "Texture coordinates").
[[nodiscard]] auto card_uvs() -> std::array<glm::vec2, 4>
{
    return std::array<glm::vec2, 4>{
        glm::vec2{1.0f, 0.0f},
        glm::vec2{0.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 1.0f}
    };
}

// The corners of one axis-aligned card, from UsdImagingDrawModeAdapter's
// _GenerateCardsGeometry: `box` puts the face on the extent's own plane,
// `cross` on the extent's mid plane, the two faces of an axis separated by an
// epsilon.
[[nodiscard]] auto axis_aligned_card(
    const Draw_mode_card_face face,
    const glm::vec3&          min,
    const glm::vec3&          max,
    const bool                cross
) -> Card_quad
{
    const glm::vec3 mid = 0.5f * (min + max);
    Card_quad quad{};
    quad.uv = card_uvs();
    switch (face) {
        case Draw_mode_card_face::x_pos: {
            const float x = cross ? (mid.x + c_cross_epsilon) : max.x;
            quad.corner = {
                glm::vec3{x, max.y, max.z}, glm::vec3{x, min.y, max.z},
                glm::vec3{x, min.y, min.z}, glm::vec3{x, max.y, min.z}
            };
            break;
        }
        case Draw_mode_card_face::x_neg: {
            const float x = cross ? (mid.x - c_cross_epsilon) : min.x;
            quad.corner = {
                glm::vec3{x, min.y, max.z}, glm::vec3{x, max.y, max.z},
                glm::vec3{x, max.y, min.z}, glm::vec3{x, min.y, min.z}
            };
            break;
        }
        case Draw_mode_card_face::y_pos: {
            const float y = cross ? (mid.y + c_cross_epsilon) : max.y;
            quad.corner = {
                glm::vec3{min.x, y, max.z}, glm::vec3{max.x, y, max.z},
                glm::vec3{max.x, y, min.z}, glm::vec3{min.x, y, min.z}
            };
            break;
        }
        case Draw_mode_card_face::y_neg: {
            const float y = cross ? (mid.y - c_cross_epsilon) : min.y;
            quad.corner = {
                glm::vec3{max.x, y, max.z}, glm::vec3{min.x, y, max.z},
                glm::vec3{min.x, y, min.z}, glm::vec3{max.x, y, min.z}
            };
            break;
        }
        case Draw_mode_card_face::z_pos: {
            const float z = cross ? (mid.z + c_cross_epsilon) : max.z;
            quad.corner = {
                glm::vec3{max.x, max.y, z}, glm::vec3{min.x, max.y, z},
                glm::vec3{min.x, min.y, z}, glm::vec3{max.x, min.y, z}
            };
            break;
        }
        default: {
            const float z = cross ? (mid.z - c_cross_epsilon) : min.z;
            quad.corner = {
                glm::vec3{min.x, max.y, z}, glm::vec3{max.x, max.y, z},
                glm::vec3{max.x, min.y, z}, glm::vec3{min.x, min.y, z}
            };
            break;
        }
    }
    return quad;
}

// The card a `worldtoscreen` matrix places: the screen-space unit quad taken
// back into the prim's space, with the adapter's own fixed UVs, flipped into
// erhe's texture coordinates.
[[nodiscard]] auto from_texture_card(const glm::mat4& world_from_screen) -> Card_quad
{
    static const std::array<glm::vec3, 4> screen_corner{
        glm::vec3{-1.0f, -1.0f, 0.0f},
        glm::vec3{-1.0f,  1.0f, 0.0f},
        glm::vec3{ 1.0f,  1.0f, 0.0f},
        glm::vec3{ 1.0f, -1.0f, 0.0f}
    };
    Card_quad quad{};
    quad.uv = std::array<glm::vec2, 4>{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{1.0f, 1.0f},
        glm::vec2{1.0f, 0.0f}
    };
    for (std::size_t i = 0; i < 4; ++i) {
        const glm::vec4 world = world_from_screen * glm::vec4{screen_corner[i], 1.0f};
        quad.corner[i] = glm::vec3{world} / world.w;
    }
    return quad;
}

// The 16 numbers of one PNG text chunk named `worldtoscreen`, read row major.
// erhe's image loader decodes texels only, so the chunk is read from the file
// itself; a PNG carrying no such chunk (which is every card image of the USD
// working group's assets) leaves the matrix untouched.
[[nodiscard]] auto read_png_world_to_screen(const std::filesystem::path& path, glm::mat4& out_matrix) -> bool
{
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return false;
    }
    std::array<char, 8> signature{};
    file.read(signature.data(), signature.size());
    static const std::array<char, 8> png_signature{
        static_cast<char>(0x89), 'P', 'N', 'G', '\r', '\n', static_cast<char>(0x1a), '\n'
    };
    if (!file || (signature != png_signature)) {
        return false;
    }
    while (file) {
        std::array<std::uint8_t, 8> header{};
        file.read(reinterpret_cast<char*>(header.data()), header.size());
        if (!file) {
            return false;
        }
        const std::uint32_t length =
            (static_cast<std::uint32_t>(header[0]) << 24) |
            (static_cast<std::uint32_t>(header[1]) << 16) |
            (static_cast<std::uint32_t>(header[2]) <<  8) |
            (static_cast<std::uint32_t>(header[3]));
        const std::string type{reinterpret_cast<const char*>(header.data()) + 4, 4};
        if ((type == "IDAT") || (type == "IEND")) {
            return false;
        }
        if ((type != "tEXt") && (type != "iTXt")) {
            file.seekg(static_cast<std::streamoff>(length) + 4, std::ios::cur);
            continue;
        }
        std::string body;
        body.resize(length);
        file.read(body.data(), static_cast<std::streamsize>(length));
        file.seekg(4, std::ios::cur);
        if (!file) {
            return false;
        }
        const std::string::size_type separator = body.find('\0');
        if ((separator == std::string::npos) || (body.compare(0, separator, c_worldtoscreen_key) != 0)) {
            continue;
        }
        // `iTXt` puts a compression flag, a compression method and two more
        // null-terminated strings between the keyword and the text; only the
        // uncompressed form is read, whose flag byte is zero.
        std::string::size_type text_begin = separator + 1;
        if (type == "iTXt") {
            if ((text_begin + 2) > body.size()) {
                return false;
            }
            if (body[text_begin] != '\0') {
                return false; // compressed
            }
            text_begin += 2;
            for (int i = 0; i < 2; ++i) {
                const std::string::size_type end = body.find('\0', text_begin);
                if (end == std::string::npos) {
                    return false;
                }
                text_begin = end + 1;
            }
        }
        std::array<float, 16> values{};
        std::size_t           value_count = 0;
        std::string::size_type cursor = text_begin;
        while ((cursor < body.size()) && (value_count < values.size())) {
            while ((cursor < body.size()) && (std::isspace(static_cast<unsigned char>(body[cursor])) || (body[cursor] == ','))) {
                ++cursor;
            }
            if (cursor >= body.size()) {
                break;
            }
            std::size_t consumed = 0;
            try {
                values[value_count] = std::stof(body.substr(cursor), &consumed);
            } catch (...) {
                return false;
            }
            if (consumed == 0) {
                return false;
            }
            cursor += consumed;
            ++value_count;
        }
        if (value_count != values.size()) {
            return false;
        }
        // USD multiplies a row vector by the matrix, so the row-major values
        // read column major are already the column-vector form glm uses.
        out_matrix = glm::make_mat4(values.data());
        return true;
    }
    return false;
}

// One card image, decoded and uploaded. Shared per scene by the file it came
// from, so a stage whose prims all name the same six images reads each once.
[[nodiscard]] auto load_card_texture(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::filesystem::path& path
) -> std::shared_ptr<erhe::graphics::Texture>
{
    const std::string key = path.generic_string();
    const std::shared_ptr<erhe::graphics::Texture> cached = scene_root.find_card_texture(key);
    if (cached) {
        return cached;
    }
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        log_scene->warn("Draw mode card texture '{}' not found", key);
        return {};
    }

    erhe::graphics::Image_loader loader;
    erhe::graphics::Image_info   info;
    const erhe::graphics::Transcode_format_preference transcode_format_preference =
        erhe::gltf::query_gltf_device_options(*context.graphics_device).transcode_format_preference;
    if (!loader.open(path, info, false, transcode_format_preference)) {
        log_scene->warn("Draw mode card texture '{}' could not be decoded", key);
        return {};
    }
    std::size_t byte_count = 0;
    if (erhe::dataformat::is_block_compressed(info.format) || (info.level_count > 1)) {
        byte_count = erhe::dataformat::get_mip_chain_byte_count(
            info.format,
            static_cast<std::size_t>(info.width),
            static_cast<std::size_t>(info.height),
            static_cast<std::size_t>(info.level_count)
        );
    } else {
        byte_count = static_cast<std::size_t>(info.row_stride) * static_cast<std::size_t>(info.height);
    }
    if (byte_count == 0) {
        loader.close();
        log_scene->warn("Draw mode card texture '{}' is empty", key);
        return {};
    }
    std::vector<std::uint8_t> pixels;
    pixels.resize(byte_count);
    const bool loaded = loader.load(std::span<std::uint8_t>{pixels.data(), pixels.size()});
    loader.close();
    if (!loaded) {
        log_scene->warn("Draw mode card texture '{}' pixels could not be read", key);
        return {};
    }

    const std::string name = erhe::file::to_string(path.filename());
    erhe::graphics::Texture_create_info texture_create_info{
        .device      = *context.graphics_device,
        .usage_mask  =
            erhe::graphics::Image_usage_flag_bit_mask::sampled |
            erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
        .pixelformat = info.format,
        .use_mipmaps = true,
        .width       = info.width,
        .height      = info.height,
        .depth       = info.depth,
        .level_count = info.level_count,
        .row_stride  = info.row_stride,
        .debug_label = erhe::utility::Debug_label{name}
    };
    const int  mipmap_count = texture_create_info.get_texture_level_count();
    const bool generate_mipmap =
        (mipmap_count != info.level_count) &&
        !erhe::dataformat::is_block_compressed(info.format);
    if (generate_mipmap) {
        texture_create_info.usage_mask |= erhe::graphics::Image_usage_flag_bit_mask::transfer_src;
        texture_create_info.level_count = mipmap_count;
    }
    std::shared_ptr<erhe::graphics::Texture> texture = std::make_shared<erhe::graphics::Texture>(
        *context.graphics_device,
        texture_create_info
    );
    texture->set_name(name);
    texture->set_source_path(path);
    {
        erhe::gltf::Image_transfer image_transfer{*context.graphics_device};
        image_transfer.upload(
            info,
            std::span<const std::uint8_t>{pixels.data(), pixels.size()},
            *texture.get(),
            generate_mipmap
        );
        image_transfer.flush();
    }
    scene_root.add_card_texture(key, texture);
    return texture;
}

// One quad, as the geometry a primitive is built from.
[[nodiscard]] auto make_card_geometry(const Card_quad& quad) -> std::shared_ptr<erhe::geometry::Geometry>
{
    std::shared_ptr<erhe::geometry::Geometry> geometry = std::make_shared<erhe::geometry::Geometry>();
    GEO::Mesh&                                mesh     = geometry->get_mesh();
    erhe::geometry::Mesh_attributes           attributes{mesh};
    mesh.vertices.create_vertices(4);
    for (GEO::index_t i = 0; i < 4; ++i) {
        const glm::vec3& corner = quad.corner[i];
        const glm::vec2& uv     = quad.uv[i];
        erhe::geometry::set_pointf(mesh.vertices, i, GEO::vec3f{corner.x, corner.y, corner.z});
        attributes.vertex_texcoord_0.set(i, GEO::vec2f{uv.x, uv.y});
    }
    mesh.facets.create_quad(0, 1, 2, 3);
    return geometry;
}

// The cutout a card image is drawn through
// (UsdImagingDrawModeAdapter::GetMaterialResource puts exactly this
// value in the surface's `opacityThreshold` when it connects the card
// texture's alpha to `opacity`).
constexpr float c_card_opacity_threshold = 0.1f;

// A card image carries the silhouette of the subtree it stands for in its
// alpha channel, and the imaging adapter cuts the card out along it: the
// texture's alpha feeds the surface's `opacity` and an `opacityThreshold` of
// 0.1 makes that a cutout rather than a blend. The erhe equivalent is the
// alpha_test blending mode with that cutoff - the fragment alpha is the
// material's opacity times the alpha channel of the base color slot, which is
// what the default `opacity_channel` names - so the card stays in the opaque
// pass, the way the adapter's cutout does. A face with no image is the flat
// draw-mode color and covers its whole quad.
[[nodiscard]] auto make_card_material(
    const std::string_view                          name,
    const glm::vec3&                                base_color,
    const std::shared_ptr<erhe::graphics::Texture>& texture
) -> std::shared_ptr<erhe::primitive::Material>
{
    std::shared_ptr<erhe::primitive::Material> material = std::make_shared<erhe::primitive::Material>(
        erhe::primitive::Material_create_info{
            .name   = std::string{name},
            .values = {
                .base_color    = base_color,
                .bxdf_model    = erhe::primitive::Bxdf_model::unlit,
                .blending_mode = texture
                    ? erhe::primitive::Material_blending_mode::alpha_test
                    : erhe::primitive::Material_blending_mode::opaque,
                .double_sided  = true,
                .alpha_cutoff  = c_card_opacity_threshold
            }
        }
    );
    if (texture) {
        material->set_base_color_texture(texture);
    }
    material->enable_flag_bits(erhe::Item_flags::draw_mode_proxy | erhe::Item_flags::session_only);
    material->disable_flag_bits(erhe::Item_flags::show_in_ui);
    return material;
}

// The face's short label, for the names of the proxy's materials.
[[nodiscard]] auto card_face_label(const Draw_mode_card_face face) -> std::string_view
{
    switch (face) {
        case Draw_mode_card_face::x_neg: return "X-";
        case Draw_mode_card_face::x_pos: return "X+";
        case Draw_mode_card_face::y_neg: return "Y-";
        case Draw_mode_card_face::y_pos: return "Y+";
        case Draw_mode_card_face::z_neg: return "Z-";
        default                        : return "Z+";
    }
}

// The faces the resolved card visibility draws, in the record's face order.
// `simple` leaves out the pair normal to the stage's up axis; erhe rotates a
// Z-up stage at its root, and the axis a file authored does not travel into
// the editor, so the up axis is Y (doc/usd_compatibility.md, "Draw modes").
[[nodiscard]] auto is_face_drawn(const Draw_mode_card_face face, const Draw_mode_card_visibility visibility) -> bool
{
    if (visibility != Draw_mode_card_visibility::simple) {
        return true;
    }
    return (face != Draw_mode_card_face::y_neg) && (face != Draw_mode_card_face::y_pos);
}

} // anonymous namespace

auto build_draw_mode_card_proxy(App_context& context, Draw_mode& draw_mode) -> std::shared_ptr<erhe::scene::Mesh>
{
    if (draw_mode.resolved_draw_mode() != erhe::scene::Draw_mode::cards) {
        return {};
    }
    erhe::Item_host* const item_host = draw_mode.get_item_host();
    if (item_host == nullptr) {
        return {};
    }
    Scene_root& scene_root = *static_cast<Scene_root*>(item_host);
    glm::vec3   min{0.0f};
    glm::vec3   max{0.0f};
    if (!draw_mode.get_extent(min, max)) {
        return {};
    }

    const Draw_mode_card_geometry   card_geometry = draw_mode.get_value(Draw_mode::card_geometry_property);
    const Draw_mode_card_visibility visibility    = draw_mode.resolved_card_visibility();
    const glm::vec3                 color         = draw_mode.get_value(Draw_mode::draw_mode_color_property);
    const std::string               owner_name    = draw_mode.get_node() != nullptr ? draw_mode.get_node()->get_name() : draw_mode.get_name();

    std::shared_ptr<erhe::scene::Mesh> proxy = std::make_shared<erhe::scene::Mesh>(owner_name + " cards");
    bool from_texture_fallback_reported = false;

    for (std::size_t i = 0; i < c_draw_mode_card_face_count; ++i) {
        const Draw_mode_card_face face = static_cast<Draw_mode_card_face>(i);
        if (!is_face_drawn(face, visibility)) {
            continue;
        }
        // The value resolved against the file that authored it: a variant
        // block's opinion travels as the relative path the file spelled.
        const std::filesystem::path              texture_path = draw_mode.resolve_card_texture_path(face);
        std::shared_ptr<erhe::graphics::Texture> texture;
        if (!texture_path.empty()) {
            texture = load_card_texture(context, scene_root, texture_path);
        }

        Card_quad quad{};
        bool      placed = false;
        if (card_geometry == Draw_mode_card_geometry::from_texture) {
            glm::mat4 screen_from_world{1.0f};
            if (!texture_path.empty() && read_png_world_to_screen(texture_path, screen_from_world)) {
                quad   = from_texture_card(glm::inverse(screen_from_world));
                placed = true;
            } else if (!from_texture_fallback_reported) {
                from_texture_fallback_reported = true;
                log_scene->warn(
                    "Draw mode of '{}' asks for 'fromTexture' cards, but its card images carry no '{}' metadata;"
                    " the cards are placed as 'box' instead",
                    owner_name,
                    c_worldtoscreen_key
                );
            }
        }
        if (!placed) {
            quad = axis_aligned_card(face, min, max, card_geometry == Draw_mode_card_geometry::cross);
        }

        std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(
            make_card_geometry(quad),
            erhe::primitive::Build_info{
                .primitive_types{.fill_triangles = true},
                .buffer_info = context.mesh_memory->make_primitive_buffer_info()
            },
            erhe::primitive::Normal_style::polygon_normals
        );
        if (!primitive->make_raytrace()) {
            log_scene->warn("Draw mode card of '{}' could not be made raytraceable", owner_name);
        }
        // A textured face shows the image as it is; a face with no image is
        // the draw-mode color, which is what the imaging adapter draws.
        std::shared_ptr<erhe::primitive::Material> material = make_card_material(
            fmt::format("{} card {}", owner_name, card_face_label(face)),
            texture ? glm::vec3{1.0f, 1.0f, 1.0f} : color,
            texture
        );
        proxy->add_primitive(primitive, material);
        // A resource is a prim where it sits (U4): the proxy's materials sit
        // under the proxy itself, which is what gives them a live home
        // without listing them in the scene's content library.
        material->set_parent(proxy);
    }

    if (proxy->get_primitives().empty()) {
        return {};
    }
    proxy->layer_id = scene_root.layers().content()->id;
    proxy->set_double_sided(true);
    // `visible` is a derived bit of the visible property, which is already
    // true by default; enable_flag_bits refuses the derived bits.
    proxy->enable_flag_bits(
        erhe::Item_flags::content         |
        erhe::Item_flags::id              |
        erhe::Item_flags::session_only    |
        erhe::Item_flags::draw_mode_proxy
    );
    proxy->disable_flag_bits(erhe::Item_flags::show_in_ui);
    return proxy;
}

} // namespace editor
