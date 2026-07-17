#include "texture_graph/nodes/texture_material_output_node.hpp"

#include "graph_editor/graph_editor_widgets.hpp"

#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_compose.hpp"
#include "texture_graph/texture_payload.hpp"
#include "texture_graph/texture_renderer.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_texgen/composer.hpp"
#include "erhe_texgen/node_descriptor.hpp"
#include "erhe_texgen/shader_code.hpp"

#include <glm/glm.hpp>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cfloat>

namespace editor {

namespace {

constexpr const char* c_size_names[]  = { "256", "512", "1024", "2048" };
constexpr int         c_size_values[] = { 256, 512, 1024, 2048 };

// First input pin slot of each channel (each channel owns two type-tagged pins,
// its primary type first). Kept in sync with the make_input_pin() calls below.
constexpr std::size_t c_pin_albedo    = 0;  // rgba, rgb
constexpr std::size_t c_pin_metallic  = 2;  // f, rgba
constexpr std::size_t c_pin_roughness = 4;  // f, rgba
constexpr std::size_t c_pin_normal    = 6;  // rgb, rgba
constexpr std::size_t c_pin_occlusion = 8;  // f, rgba
constexpr std::size_t c_pin_emissive  = 10; // rgb, rgba

// Synthetic combiner descriptor that packs three grayscale channels into one
// glTF ORM (occlusion-roughness-metallic) rgb texture. R = occlusion,
// G = roughness, B = metallic. Defaults are chosen so an unconnected channel,
// combined with the scalar multiplier the material carries (metallic = 1,
// roughness = vec2(1)), yields the sensible fallback: occlusion default 1.0
// (fully lit; the occlusion slot is only assigned when occlusion is connected),
// roughness default 1.0 (Material Maker's default), metallic default 0.0
// (dielectric). Not registered in the node factory / palette - built and owned
// here, referenced only during composition.
[[nodiscard]] auto orm_combiner_descriptor() -> const erhe::texgen::Node_descriptor&
{
    static const erhe::texgen::Node_descriptor descriptor = [] {
        erhe::texgen::Node_descriptor d{};
        d.name  = "material_orm_combiner";
        d.label = "ORM";
        d.inputs = {
            erhe::texgen::Input_descriptor{.name = "occlusion", .type = erhe::texgen::Value_type::grayscale, .default_expression = "1.0"},
            erhe::texgen::Input_descriptor{.name = "roughness", .type = erhe::texgen::Value_type::grayscale, .default_expression = "1.0"},
            erhe::texgen::Input_descriptor{.name = "metallic",  .type = erhe::texgen::Value_type::grayscale, .default_expression = "0.0"}
        };
        d.outputs = {
            erhe::texgen::Output_descriptor{.type = erhe::texgen::Value_type::rgb, .expression = "vec3($occlusion($uv), $roughness($uv), $metallic($uv))"}
        };
        return d;
    }();
    return descriptor;
}

// Composes a single connected channel subtree into a fragment body + uniforms.
// Returns false (and leaves fragment / uniforms untouched) when the subtree
// does not compose (unconnected, missing descriptor, or an error marker).
[[nodiscard]] auto compose_channel(
    const Texture_payload&               source,
    std::string&                         out_fragment,
    std::vector<erhe::texgen::Uniform>&  out_uniforms
) -> bool
{
    if (source.source_node == nullptr) {
        return false;
    }
    const Texture_compose_dag dag = build_texture_compose_dag(*source.source_node, source.output_index);
    if (!dag.ok || (dag.sink == nullptr)) {
        return false;
    }
    const erhe::texgen::Composer    composer{texture_graph_compose_options()};
    const erhe::texgen::Shader_code shader_code = composer.compose(*dag.sink, dag.sink_output_index);
    const std::string               fragment    = composer.assemble_fragment(shader_code);
    if (fragment.find("(error:") != std::string::npos) {
        return false;
    }
    out_fragment = fragment;
    out_uniforms = shader_code.get_uniforms();
    return true;
}

// Composes the packed ORM texture (any of occlusion / roughness / metallic
// connected). Returns false when none are connected or composition failed.
[[nodiscard]] auto compose_orm(
    const Texture_payload&              occlusion,
    const Texture_payload&              roughness,
    const Texture_payload&              metallic,
    std::string&                        out_fragment,
    std::vector<erhe::texgen::Uniform>& out_uniforms
) -> bool
{
    if ((occlusion.source_node == nullptr) && (roughness.source_node == nullptr) && (metallic.source_node == nullptr)) {
        return false;
    }
    const std::vector<Texture_payload> roots{occlusion, roughness, metallic};
    const Texture_compose_dag dag = build_texture_combiner_dag(orm_combiner_descriptor(), roots);
    if (!dag.ok || (dag.sink == nullptr)) {
        return false;
    }
    const erhe::texgen::Composer    composer{texture_graph_compose_options()};
    const erhe::texgen::Shader_code shader_code = composer.compose(*dag.sink, dag.sink_output_index);
    const std::string               fragment    = composer.assemble_fragment(shader_code);
    if (fragment.find("(error:") != std::string::npos) {
        return false;
    }
    out_fragment = fragment;
    out_uniforms = shader_code.get_uniforms();
    return true;
}

} // namespace

Texture_material_output_node::Texture_material_output_node(App_context& context)
    : Texture_graph_node{"Material"}
    , m_context         {context}
{
    // Two pins per PBR channel so common generator output types connect (the
    // primary type is listed first; pick_channel() prefers it). Keep this in
    // lockstep with the c_pin_* slot constants above.
    make_input_pin(Texture_pin_key::rgba,      "Albedo");
    make_input_pin(Texture_pin_key::rgb,       "Albedo rgb");
    make_input_pin(Texture_pin_key::grayscale, "Metallic");
    make_input_pin(Texture_pin_key::rgba,      "Metallic rgba");
    make_input_pin(Texture_pin_key::grayscale, "Roughness");
    make_input_pin(Texture_pin_key::rgba,      "Roughness rgba");
    make_input_pin(Texture_pin_key::rgb,       "Normal");
    make_input_pin(Texture_pin_key::rgba,      "Normal rgba");
    make_input_pin(Texture_pin_key::grayscale, "Occlusion");
    make_input_pin(Texture_pin_key::rgba,      "Occlusion rgba");
    make_input_pin(Texture_pin_key::rgb,       "Emissive");
    make_input_pin(Texture_pin_key::rgba,      "Emissive rgba");
    m_material_reference.set_user_label("graph material output material");
}

Texture_material_output_node::~Texture_material_output_node() noexcept
{
    for (Baked_texture& slot : m_separate) {
        unregister_texture(slot);
    }
    unregister_orm();
}

auto Texture_material_output_node::get_material() const -> std::shared_ptr<erhe::primitive::Material>
{
    return m_material_reference.get_as<erhe::primitive::Material>();
}

void Texture_material_output_node::resolve_reference()
{
    // Main thread only (the manager verifies). scene_local misses do not
    // latch, so calls double as the retry. Deliberately does NOT mark the
    // node dirty: the render / assignment path consumes the just-resolved
    // material in the same pass, and a lingering dirty flag would cause a
    // spurious extra bake. The imgui retry marks dirty itself on the
    // resolve transition.
    if (m_material_reference.get() || m_material_reference.get_key().name.empty()) {
        return;
    }
    m_material_reference.resolve(*m_context.asset_manager);
}

void Texture_material_output_node::on_removed_from_graph()
{
    for (Baked_texture& slot : m_separate) {
        unregister_texture(slot);
    }
    unregister_orm();
    // Only a resolved material can carry this node's assignments; an
    // unresolved reference never assigned anything, so nothing to clear.
    const std::shared_ptr<erhe::primitive::Material> material = get_material();
    if (m_assign_to_material && material) {
        erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
        samplers.base_color.texture_reference.reset();
        samplers.base_color.sampler.reset();
        samplers.normal.texture_reference.reset();
        samplers.normal.sampler.reset();
        samplers.emissive.texture_reference.reset();
        samplers.emissive.sampler.reset();
        samplers.metallic_roughness.texture_reference.reset();
        samplers.metallic_roughness.sampler.reset();
        samplers.occlusion.texture_reference.reset();
        samplers.occlusion.sampler.reset();
        material->data.metallic  = 0.0f;
        material->data.roughness = glm::vec2{0.5f, 0.5f};
        material->data.emissive  = glm::vec3{0.0f, 0.0f, 0.0f};
    }
}

auto Texture_material_output_node::render_target_size() const -> int
{
    return c_size_values[std::clamp(m_size_index, 0, 3)];
}

auto Texture_material_output_node::get_base_name() const -> const std::string&
{
    return m_base_name;
}

auto Texture_material_output_node::pick_channel(const std::size_t first_pin) const -> Texture_payload
{
    // Prefer the channel's primary input pin (listed first); fall back to its
    // secondary pin.
    for (std::size_t offset = 0; offset < 2; ++offset) {
        const Texture_payload source = input_from_links(first_pin + offset);
        if (source.source_node != nullptr) {
            return source;
        }
    }
    return Texture_payload{};
}

void Texture_material_output_node::evaluate(Texture_graph&)
{
    pull_inputs();
}

auto Texture_material_output_node::ensure_sampler() -> const std::shared_ptr<erhe::graphics::Sampler>&
{
    if (!m_sampler && (m_context.graphics_device != nullptr)) {
        m_sampler = std::make_shared<erhe::graphics::Sampler>(
            *m_context.graphics_device,
            erhe::graphics::Sampler_create_info{
                .min_filter  = erhe::graphics::Filter::linear,
                .mag_filter  = erhe::graphics::Filter::linear,
                .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
                .debug_label = erhe::utility::Debug_label{"Texture material output sampler"}
            }
        );
    }
    return m_sampler;
}

auto Texture_material_output_node::resolve_scene_root() -> std::shared_ptr<Scene_root>
{
    // Drop a stored selection whose scene was closed (m_scene_root is weak,
    // but the Scene_root can be pinned past its close by other holders).
    std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (scene_root && !m_context.app_scenes->is_host_registered(scene_root.get())) {
        scene_root.reset();
        m_scene_root.reset();
    }
    if (!scene_root) {
        // The owning Graph_texture asset lives in exactly one scene's content
        // library, and library items are hosted by their owning Scene_root -
        // resolve through the asset so this works with any number of scenes
        // open (the old get_single_scene_root() returned null for >1 scene).
        scene_root = get_hosting_scene_root(get_owning_graph_texture().get());
    }
    if (!scene_root) {
        scene_root = m_context.app_scenes->get_single_scene_root();
    }
    if (scene_root) {
        m_scene_root = scene_root;
    }
    return scene_root;
}

auto Texture_material_output_node::get_content_library() -> std::shared_ptr<Content_library>
{
    const std::shared_ptr<Scene_root> scene_root = resolve_scene_root();
    if (!scene_root) {
        return {};
    }
    const std::shared_ptr<Content_library> library = scene_root->get_content_library();
    if (library && library->textures) {
        return library;
    }
    return {};
}

void Texture_material_output_node::register_texture(Baked_texture& slot, const std::string& name)
{
    if (!slot.target) {
        return;
    }
    slot.target->set_name(name);
    const std::shared_ptr<Content_library> library = get_content_library();
    if (!library) {
        return;
    }
    if (slot.registered == slot.target) {
        return; // same object, only its contents were re-rendered
    }
    if (slot.registered) {
        library->textures->remove(slot.registered);
    }
    library->textures->add(slot.target);
    slot.registered = slot.target;
}

void Texture_material_output_node::unregister_texture(Baked_texture& slot)
{
    // Unregister from the scene the texture was registered into (the stored
    // selection), without re-resolving; a scene that is already gone has
    // nothing to unregister from.
    const std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (slot.registered && scene_root) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library && library->textures) {
            library->textures->remove(slot.registered);
        }
    }
    slot.registered.reset();
}

void Texture_material_output_node::unregister_orm()
{
    const std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (m_orm_registered && scene_root) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library && library->textures) {
            library->textures->remove(m_orm_registered);
        }
    }
    m_orm_registered.reset();
}

void Texture_material_output_node::render_separate_channel(
    Texture_renderer&      renderer,
    const Separate_channel channel,
    const Texture_payload& source,
    const int              size
)
{
    const std::size_t channel_index = static_cast<std::size_t>(channel);
    Baked_texture&    slot          = m_separate[channel_index];

    const std::shared_ptr<erhe::primitive::Material> material = get_material();
    erhe::primitive::Material_texture_sampler* sampler_slot = nullptr;
    const char*                                suffix       = "";
    if (material) {
        erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
        switch (channel) {
            case Separate_channel::albedo:   sampler_slot = &samplers.base_color; suffix = "Albedo";   break;
            case Separate_channel::normal:   sampler_slot = &samplers.normal;     suffix = "Normal";   break;
            case Separate_channel::emissive: sampler_slot = &samplers.emissive;   suffix = "Emissive"; break;
            default: break;
        }
    } else {
        switch (channel) {
            case Separate_channel::albedo:   suffix = "Albedo";   break;
            case Separate_channel::normal:   suffix = "Normal";   break;
            case Separate_channel::emissive: suffix = "Emissive"; break;
            default: break;
        }
    }

    std::string                        fragment;
    std::vector<erhe::texgen::Uniform> uniforms;
    const bool connected = compose_channel(source, fragment, uniforms);
    if (!connected) {
        // Unconnected (or failed compose): drop the baked texture and revert the
        // material slot to its scalar default.
        unregister_texture(slot);
        slot.target.reset();
        if (m_assign_to_material && (sampler_slot != nullptr)) {
            sampler_slot->texture_reference.reset();
            sampler_slot->sampler.reset();
            if (channel == Separate_channel::emissive) {
                material->data.emissive = glm::vec3{0.0f, 0.0f, 0.0f};
            }
        }
        return;
    }

    if (m_context.current_command_buffer == nullptr) {
        return;
    }
    const bool ok = renderer.render_into(*m_context.current_command_buffer, slot.target, size, fragment, uniforms);
    if (!ok) {
        return; // compile failure - keep the previous texture / assignment
    }
    register_texture(slot, m_base_name + " " + suffix);

    if (m_assign_to_material && (sampler_slot != nullptr) && slot.target) {
        sampler_slot->texture_reference = slot.target;
        sampler_slot->sampler = ensure_sampler();
        if (channel == Separate_channel::albedo) {
            material->data.base_color = glm::vec3{1.0f, 1.0f, 1.0f};
        } else if (channel == Separate_channel::normal) {
            material->data.normal_texture_scale = 1.0f;
        } else if (channel == Separate_channel::emissive) {
            material->data.emissive = glm::vec3{1.0f, 1.0f, 1.0f};
        }
    }

    // Show the albedo bake as the node thumbnail (base draw_preview reads it).
    if (channel == Separate_channel::albedo) {
        m_preview_texture = slot.target;
    }
}

void Texture_material_output_node::render_orm(
    Texture_renderer&      renderer,
    const Texture_payload& occlusion,
    const Texture_payload& roughness,
    const Texture_payload& metallic,
    const int              size
)
{
    const std::shared_ptr<erhe::primitive::Material> material = get_material();
    std::string                        fragment;
    std::vector<erhe::texgen::Uniform> uniforms;
    const bool any = compose_orm(occlusion, roughness, metallic, fragment, uniforms);
    if (!any) {
        unregister_orm();
        m_orm_target.reset();
        if (m_assign_to_material && material) {
            erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
            samplers.metallic_roughness.texture_reference.reset();
            samplers.metallic_roughness.sampler.reset();
            samplers.occlusion.texture_reference.reset();
            samplers.occlusion.sampler.reset();
            material->data.metallic  = 0.0f;
            material->data.roughness = glm::vec2{0.5f, 0.5f};
        }
        return;
    }

    if (m_context.current_command_buffer == nullptr) {
        return;
    }
    const bool ok = renderer.render_into(*m_context.current_command_buffer, m_orm_target, size, fragment, uniforms);
    if (!ok) {
        return;
    }
    // Register the ORM texture.
    if (m_orm_target) {
        m_orm_target->set_name(m_base_name + " ORM");
        const std::shared_ptr<Content_library> library = get_content_library();
        if (library) {
            if (m_orm_registered != m_orm_target) {
                if (m_orm_registered) {
                    library->textures->remove(m_orm_registered);
                }
                library->textures->add(m_orm_target);
                m_orm_registered = m_orm_target;
            }
        }
    }

    if (m_assign_to_material && material && m_orm_target) {
        erhe::primitive::Material_texture_samplers& samplers = material->data.texture_samplers;
        // Packed texture drives roughness (.g) and metallic (.b); the scalar
        // multipliers pass the baked values through unchanged.
        samplers.metallic_roughness.texture_reference = m_orm_target;
        samplers.metallic_roughness.sampler = ensure_sampler();
        material->data.metallic  = 1.0f;
        material->data.roughness = glm::vec2{1.0f, 1.0f};

        // erhe samples occlusion from a separate slot (.r); assign the same
        // packed texture there only when occlusion is actually connected, else
        // leave occlusion at the shader default of 1.0.
        if (occlusion.source_node != nullptr) {
            samplers.occlusion.texture_reference = m_orm_target;
            samplers.occlusion.sampler = ensure_sampler();
            material->data.occlusion_texture_strength = 1.0f;
        } else {
            samplers.occlusion.texture_reference.reset();
            samplers.occlusion.sampler.reset();
        }
    }
}

void Texture_material_output_node::render_products(App_context& context, Texture_renderer& renderer)
{
    static_cast<void>(context);
    if (m_context.current_command_buffer == nullptr) {
        return;
    }
    resolve_reference(); // main thread; deferred material key resolves here at the latest

    const int size = render_target_size();

    render_separate_channel(renderer, Separate_channel::albedo,   pick_channel(c_pin_albedo),   size);
    render_separate_channel(renderer, Separate_channel::normal,   pick_channel(c_pin_normal),   size);
    render_separate_channel(renderer, Separate_channel::emissive, pick_channel(c_pin_emissive), size);
    render_orm(
        renderer,
        pick_channel(c_pin_occlusion),
        pick_channel(c_pin_roughness),
        pick_channel(c_pin_metallic),
        size
    );

    // If albedo is unconnected, fall back to the ORM texture for the thumbnail.
    if (!m_separate[static_cast<std::size_t>(Separate_channel::albedo)].target) {
        m_preview_texture = m_orm_target;
    }
}

auto Texture_material_output_node::build_channel_exports() const -> std::vector<Material_channel_export>
{
    std::vector<Material_channel_export> exports;

    const Texture_payload albedo    = pick_channel(c_pin_albedo);
    const Texture_payload normal    = pick_channel(c_pin_normal);
    const Texture_payload emissive  = pick_channel(c_pin_emissive);
    const Texture_payload occlusion = pick_channel(c_pin_occlusion);
    const Texture_payload roughness = pick_channel(c_pin_roughness);
    const Texture_payload metallic  = pick_channel(c_pin_metallic);

    auto push_separate = [&exports](const char* suffix, const Texture_payload& source) {
        Material_channel_export entry{};
        if (compose_channel(source, entry.fragment, entry.uniforms)) {
            entry.suffix = suffix;
            exports.push_back(std::move(entry));
        }
    };
    push_separate("albedo",   albedo);
    push_separate("normal",   normal);
    push_separate("emissive", emissive);
    push_separate("occlusion", occlusion);

    Material_channel_export orm{};
    if (compose_orm(occlusion, roughness, metallic, orm.fragment, orm.uniforms)) {
        orm.suffix = "metallic_roughness";
        exports.push_back(std::move(orm));
    }
    return exports;
}

void Texture_material_output_node::imgui()
{
    // Base name (committed on defocus).
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##base_name", &m_base_name);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        mark_dirty();
    }

    if (imgui_enum_stepper("size", m_size_index, c_size_names, 4)) {
        mark_dirty();
    }

    // Scene selection (only when several scenes exist).
    const std::vector<std::shared_ptr<Scene_root>>& scene_roots = m_context.app_scenes->get_scene_roots();
    if (scene_roots.size() > 1) {
        std::shared_ptr<Scene_root> current_scene_root = m_scene_root.lock();
        int scene_index = 0;
        for (std::size_t i = 0, end = scene_roots.size(); i < end; ++i) {
            if (scene_roots[i] == current_scene_root) {
                scene_index = static_cast<int>(i);
                break;
            }
        }
        if (imgui_index_stepper("scene", scene_index, static_cast<int>(scene_roots.size()))) {
            for (Baked_texture& slot : m_separate) {
                unregister_texture(slot);
            }
            unregister_orm();
            current_scene_root = scene_roots.at(static_cast<std::size_t>(scene_index));
            m_scene_root = current_scene_root;
            m_material_reference.set_key(Asset_key{});
            mark_dirty();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(current_scene_root ? current_scene_root->get_name().c_str() : "(no scene)");
    }

    // Per-frame retry of a deferred / broken reference while the node is
    // visible (the R2 slot-resolve cadence; scene_local misses do not latch).
    const bool was_unresolved = (m_material_reference.get() == nullptr) && !m_material_reference.get_key().name.empty();
    resolve_reference();
    const std::shared_ptr<erhe::primitive::Material> current_material = get_material();
    if (was_unresolved && current_material) {
        mark_dirty(); // re-bake to apply the assignment the unresolved bake could not make
    }

    // Material selection.
    std::shared_ptr<Scene_root> selection_root = m_scene_root.lock();
    if (!selection_root) {
        selection_root = m_context.app_scenes->get_single_scene_root();
    }
    if (selection_root) {
        const std::shared_ptr<Content_library> library = selection_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                int material_index = 0;
                for (std::size_t i = 0, end = materials.size(); i < end; ++i) {
                    if (materials[i] == current_material) {
                        material_index = static_cast<int>(i);
                        break;
                    }
                }
                if (imgui_index_stepper("material", material_index, static_cast<int>(materials.size()))) {
                    m_material_reference.adopt(*m_context.asset_manager, materials.at(static_cast<std::size_t>(material_index)));
                    mark_dirty();
                }
                ImGui::SameLine();
                if (current_material) {
                    ImGui::TextUnformatted(current_material->get_name().c_str());
                } else if (!m_material_reference.get_key().name.empty()) {
                    ImGui::Text("(unresolved: %s)", m_material_reference.get_key().name.c_str());
                } else {
                    ImGui::TextUnformatted("(no material)");
                }
            }
        }

        // Create a fresh material and select it (not undoable; a content-library
        // edit, like the geometry output node's assignment side effects).
        if (library && library->materials && ImGui::Button("New Material")) {
            m_material_reference.adopt(
                *m_context.asset_manager,
                library->materials->make<erhe::primitive::Material>(
                    erhe::primitive::Material_create_info{.name = m_base_name}
                )
            );
            mark_dirty();
        }
    }

    if (ImGui::Checkbox("Assign to material", &m_assign_to_material)) {
        mark_dirty();
    }
}

void Texture_material_output_node::write_parameters(nlohmann::json& out) const
{
    out["name"]   = m_base_name;
    out["size"]   = render_target_size();
    out["assign"] = m_assign_to_material;
    const std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (scene_root) {
        out["scene"] = scene_root->get_name();
    }
    // The key is written even while unresolved (R4: no silent loss).
    const std::string& material_name = m_material_reference.get_key().name;
    if (!material_name.empty()) {
        out["material"] = material_name;
    }
}

void Texture_material_output_node::read_parameters(const nlohmann::json& in)
{
    m_base_name = in.value("name", m_base_name);
    if (in.contains("size")) {
        const int size = in["size"].get<int>();
        for (int i = 0; i < 4; ++i) {
            if (c_size_values[i] == size) {
                m_size_index = i;
                break;
            }
        }
    }
    m_assign_to_material = in.value("assign", m_assign_to_material);

    const std::string scene_name = in.value("scene", "");
    if (!scene_name.empty()) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (scene_root->get_name() == scene_name) {
                m_scene_root = scene_root;
                break;
            }
        }
    }
    if (in.contains("material")) {
        // Store the key only; resolution is deferred to the main thread
        // (render_products / imgui resolve through the manager).
        Asset_key key;
        key.scope = Asset_scope::scene_local;
        key.type  = Asset_type::material;
        key.name  = in.value("material", std::string{});
        m_material_reference.set_key(key);
    }
    mark_dirty();
}

} // namespace editor
