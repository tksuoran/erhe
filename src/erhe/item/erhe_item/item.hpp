#pragma once

#include "erhe_item/unique_id.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace erhe {

class Item_host;

class Item_flags
{
public:
    static constexpr uint64_t none                      = 0u;
    static constexpr uint64_t no_message                = (1u <<  0);
    static constexpr uint64_t no_transform_update       = (1u <<  1);
    static constexpr uint64_t transform_world_normative = (1u <<  2);
    static constexpr uint64_t show_in_ui                = (1u <<  3);
    static constexpr uint64_t show_debug_visualizations = (1u <<  4);
    static constexpr uint64_t shadow_cast               = (1u <<  5);
    static constexpr uint64_t selected                  = (1u <<  6);
    static constexpr uint64_t lock_viewport_selection   = (1u <<  7);
    static constexpr uint64_t lock_viewport_transform   = (1u <<  8);
    static constexpr uint64_t visible                   = (1u <<  9);
    static constexpr uint64_t invisible_parent          = (1u << 10);
    static constexpr uint64_t opaque                    = (1u << 11);
    static constexpr uint64_t translucent               = (1u << 12);
    static constexpr uint64_t render_wireframe          = (1u << 13); // TODO
    static constexpr uint64_t render_bounding_volume    = (1u << 14); // TODO
    static constexpr uint64_t content                   = (1u << 15);
    static constexpr uint64_t id                        = (1u << 16);
    static constexpr uint64_t tool                      = (1u << 17);
    static constexpr uint64_t brush                     = (1u << 18);
    static constexpr uint64_t controller                = (1u << 19);
    static constexpr uint64_t rendertarget              = (1u << 20);
    static constexpr uint64_t count                     = 21;

    static constexpr const char* c_bit_labels[] =
    {
        "No Message",
        "No Transform Update",
        "Transform World Normative",
        "Show In UI",
        "Show Debug",
        "Shadow Cast",
        "Selected",
        "Lock Selection",
        "Lock Transform",
        "Visible",
        "Invisible Parent",
        "Opaque",
        "Translucent",
        "Render Wireframe",
        "Render Bounding Volume",
        "Content",
        "ID",
        "Tool",
        "Brush",
        "Controller",
        "Rendertarget"
    };

    [[nodiscard]] static auto to_string(uint64_t mask) -> std::string;
};

class Item_type
{
public:
    static constexpr uint64_t index_animation              =  1;
    static constexpr uint64_t index_animation_channel      =  2;
    static constexpr uint64_t index_animation_sampler      =  3;
    static constexpr uint64_t index_bone                   =  4;
    static constexpr uint64_t index_brush                  =  5;
    static constexpr uint64_t index_camera                 =  6;
    static constexpr uint64_t index_composer               =  7;
    static constexpr uint64_t index_frame_controller       =  8;
    static constexpr uint64_t index_grid                   =  9;
    static constexpr uint64_t index_light                  = 10;
    static constexpr uint64_t index_light_layer            = 11;
    static constexpr uint64_t index_material               = 12;
    static constexpr uint64_t index_mesh                   = 13;
    static constexpr uint64_t index_mesh_layer             = 14;
    static constexpr uint64_t index_renderpass             = 15;
    static constexpr uint64_t index_rendertarget           = 16;
    static constexpr uint64_t index_scene                  = 17;
    static constexpr uint64_t index_skin                   = 18;
    static constexpr uint64_t index_texture                = 19;
    static constexpr uint64_t index_node                   = 20;
    static constexpr uint64_t index_asset_folder           = 21;
    static constexpr uint64_t index_asset_file_gltf        = 22;
    static constexpr uint64_t index_asset_file_geogram     = 23;
    static constexpr uint64_t index_asset_file_other       = 24;
    static constexpr uint64_t index_content_library_folder = 25;
    static constexpr uint64_t index_content_library_node   = 26;
    static constexpr uint64_t index_physics                = 27;
    static constexpr uint64_t index_raytrace               = 28;
    static constexpr uint64_t index_node_attachment        = 29;
    static constexpr uint64_t index_brush_placement        = 30;
    static constexpr uint64_t index_render_style           = 31;
    static constexpr uint64_t index_graph                  = 32;
    static constexpr uint64_t index_graph_node             = 33;
    static constexpr uint64_t index_graph_link             = 34;
    static constexpr uint64_t count                        = 35;

    static constexpr uint64_t none                   =  uint64_t{0};
    static constexpr uint64_t animation              = (uint64_t{1} << index_animation             );
    static constexpr uint64_t animation_channel      = (uint64_t{1} << index_animation_channel     );
    static constexpr uint64_t animation_sampler      = (uint64_t{1} << index_animation_sampler     );
    static constexpr uint64_t bone                   = (uint64_t{1} << index_bone                  );
    static constexpr uint64_t brush                  = (uint64_t{1} << index_brush                 );
    static constexpr uint64_t camera                 = (uint64_t{1} << index_camera                );
    static constexpr uint64_t composer               = (uint64_t{1} << index_composer              );
    static constexpr uint64_t frame_controller       = (uint64_t{1} << index_frame_controller      );
    static constexpr uint64_t grid                   = (uint64_t{1} << index_grid                  );
    static constexpr uint64_t light                  = (uint64_t{1} << index_light                 );
    static constexpr uint64_t light_layer            = (uint64_t{1} << index_light_layer           );
    static constexpr uint64_t material               = (uint64_t{1} << index_material              );
    static constexpr uint64_t mesh                   = (uint64_t{1} << index_mesh                  );
    static constexpr uint64_t mesh_layer             = (uint64_t{1} << index_mesh_layer            );
    static constexpr uint64_t renderpass             = (uint64_t{1} << index_renderpass            );
    static constexpr uint64_t rendertarget           = (uint64_t{1} << index_rendertarget          );
    static constexpr uint64_t scene                  = (uint64_t{1} << index_scene                 );
    static constexpr uint64_t skin                   = (uint64_t{1} << index_skin                  );
    static constexpr uint64_t texture                = (uint64_t{1} << index_texture               );
    static constexpr uint64_t node                   = (uint64_t{1} << index_node                  );
    static constexpr uint64_t asset_folder           = (uint64_t{1} << index_asset_folder          );
    static constexpr uint64_t asset_file_gltf        = (uint64_t{1} << index_asset_file_gltf       );
    static constexpr uint64_t asset_file_geogram     = (uint64_t{1} << index_asset_file_geogram    );
    static constexpr uint64_t asset_file_other       = (uint64_t{1} << index_asset_file_other      );
    static constexpr uint64_t content_library_folder = (uint64_t{1} << index_content_library_folder);
    static constexpr uint64_t content_library_node   = (uint64_t{1} << index_content_library_node  );
    static constexpr uint64_t physics                = (uint64_t{1} << index_physics               );
    static constexpr uint64_t raytrace               = (uint64_t{1} << index_raytrace              );
    static constexpr uint64_t node_attachment        = (uint64_t{1} << index_node_attachment       );
    static constexpr uint64_t brush_placement        = (uint64_t{1} << index_brush_placement       );
    static constexpr uint64_t render_style           = (uint64_t{1} << index_render_style          );
    static constexpr uint64_t graph                  = (uint64_t{1} << index_graph                 );
    static constexpr uint64_t graph_node             = (uint64_t{1} << index_graph_node            );
    static constexpr uint64_t graph_link             = (uint64_t{1} << index_graph_link            );

    // NOTE: The names here must match the C++ class names
    static constexpr const char* c_bit_labels[] = {
        "none",
        "Animation",
        "Animation_channel",
        "Animation_sampler",
        "Bone",
        "Brush",
        "Camera",
        "Composer",
        "Frame_controller",
        "Grid",
        "Light",
        "Light_layer",
        "Material",
        "Mesh",
        "Mesh_layer",
        "Renderpass",
        "Rendertarget",
        "Scene",
        "Skin",
        "Texture",
        "Node",
        "Asset_folder",
        "Asset_file_gltf",
        "Asset_file_other",
        "Content_library_folder",
        "Content_library_node",
        "Physics",
        "Raytrace",
        "Node_attachment",
        "Brush_placement",
        "Render_style",
        "Graph",
        "Graph_node",
        "Graph_link",
        "Graph_link"
    };
};

class Item_filter
{
public:
    [[nodiscard]] auto operator()(uint64_t filter_bits) const -> bool;

    [[nodiscard]] auto describe() const -> std::string;

    uint64_t require_all_bits_set          {0};
    uint64_t require_at_least_one_bit_set  {0};
    uint64_t require_all_bits_clear        {0};
    uint64_t require_at_least_one_bit_clear{0};
};

// https://herbsutter.com/2019/10/03/gotw-ish-solution-the-clonable-pattern/

enum class Item_kind : unsigned int {
    clone_using_copy_constructor = 0,
    clone_using_custom_clone_constructor,
    not_clonable
};
using for_clone = bool;

template <typename T>
class Clonable_base
{
public:
    [[nodiscard]] virtual auto clone() const -> std::shared_ptr<T> {
        return std::make_shared<T>(static_cast<const T&>(*this));
    }
};

template <typename Base, typename Intermediate, typename Self, Item_kind kind = Item_kind::clone_using_copy_constructor>
class Item : public Intermediate
{
public:
    using Intermediate::Intermediate;
    auto clone() const -> std::shared_ptr<Base> override {
        if constexpr (kind == Item_kind::clone_using_copy_constructor) {
            return std::make_shared<Self>(static_cast<const Self&>(*this));
        } else if constexpr(kind == Item_kind::clone_using_custom_clone_constructor) {
            return std::make_shared<Self>(static_cast<const Self&>(*this), for_clone{});
        } else { // if constexpr(kind == Item_kind::not_clonable) {
            return std::shared_ptr<Base>{};
        }
    }
};

class Item_base
    : public std::enable_shared_from_this<Item_base>
    , public Clonable_base<Item_base>
{
public:
    Item_base();

    explicit Item_base(const std::string_view name);
    explicit Item_base(const Item_base& other);
    Item_base& operator=(const Item_base& other);
    virtual ~Item_base() noexcept;

    [[nodiscard]] virtual auto get_type     () const -> uint64_t         { return 0; };
    [[nodiscard]] virtual auto get_type_name() const -> std::string_view { return "Item_base"; };
    [[nodiscard]] virtual auto get_item_host() const -> Item_host*       { return nullptr; }

    virtual void handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits) {
        static_cast<void>(old_flag_bits);
        static_cast<void>(new_flag_bits);
    }

    [[nodiscard]] auto get_id                      () const -> std::size_t;
    [[nodiscard]] auto get_flag_bits               () const -> uint64_t;
    [[nodiscard]] auto is_no_transform_update      () const -> bool;
    [[nodiscard]] auto is_transform_world_normative() const -> bool;
    [[nodiscard]] auto is_selected                 () const -> bool;
    [[nodiscard]] auto is_visible                  () const -> bool;
    [[nodiscard]] auto is_shown_in_ui              () const -> bool;
    [[nodiscard]] auto is_hidden                   () const -> bool;
    [[nodiscard]] auto get_source_path             () const -> const std::filesystem::path&;
    [[nodiscard]] auto get_name                    () const -> const std::string&;
    [[nodiscard]] auto get_label                   () const -> const std::string&;
    [[nodiscard]] auto describe                    (int level = 0) const -> std::string;

    void set_flag_bits    (uint64_t mask, bool value);
    void enable_flag_bits (uint64_t mask);
    void disable_flag_bits(uint64_t mask);
    void set_name         (const std::string_view name);
    void set_selected     (bool value);
    void set_visible      (bool value);
    void show             ();
    void hide             ();
    void set_source_path  (const std::filesystem::path& path);

protected:
    Unique_id<Item_base>  m_id         {};
    uint64_t              m_flag_bits  {Item_flags::none};
    std::string           m_name       {};
    std::string           m_label      {};
    std::filesystem::path m_source_path{};
};

template <typename T>
auto is(const erhe::Item_base* const base) -> bool
{
    auto ptr = dynamic_cast<const T*>(base);
    return ptr != nullptr;
}

template <typename T>
auto is(const std::shared_ptr<erhe::Item_base>& base) -> bool
{
    return static_cast<bool>(std::dynamic_pointer_cast<T>(base));
}

} // namespace erhe
