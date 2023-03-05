#pragma once

#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace erhe::scene
{

class Node;
class Scene;
class Scene_host;

class Item_flags
{
public:
    static constexpr uint64_t none                      = 0u;
    static constexpr uint64_t no_message                = (1u <<  0);
    static constexpr uint64_t show_in_ui                = (1u <<  1);
    static constexpr uint64_t show_debug_visualizations = (1u <<  2);
    static constexpr uint64_t shadow_cast               = (1u <<  3);
    static constexpr uint64_t selected                  = (1u <<  4);
    static constexpr uint64_t visible                   = (1u <<  5);
    static constexpr uint64_t opaque                    = (1u <<  6);
    static constexpr uint64_t translucent               = (1u <<  7);
    static constexpr uint64_t render_wireframe          = (1u <<  8); // TODO
    static constexpr uint64_t render_bounding_volume    = (1u <<  9); // TODO
    static constexpr uint64_t content                   = (1u << 10);
    static constexpr uint64_t id                        = (1u << 11);
    static constexpr uint64_t tool                      = (1u << 12);
    static constexpr uint64_t brush                     = (1u << 13);
    static constexpr uint64_t controller                = (1u << 14);
    static constexpr uint64_t rendertarget              = (1u << 15);
    static constexpr uint64_t count                     = 16;

    static constexpr const char* c_bit_labels[] =
    {
        "No Message",
        "Show In UI",
        "Show Debug",
        "Shadow Cast",
        "Selected",
        "Visible",
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
    static constexpr uint64_t none             =  0u;
    static constexpr uint64_t scene            = (1u <<  1);
    static constexpr uint64_t mesh_layer       = (1u <<  2);
    static constexpr uint64_t light_layer      = (1u <<  3);
    static constexpr uint64_t content_folder   = (1u <<  4);
    static constexpr uint64_t brush            = (1u <<  5);
    static constexpr uint64_t material         = (1u <<  6);
    static constexpr uint64_t node             = (1u <<  7);
    static constexpr uint64_t node_attachment  = (1u <<  8);
    static constexpr uint64_t mesh             = (1u <<  9);
    static constexpr uint64_t camera           = (1u << 10);
    static constexpr uint64_t light            = (1u << 11);
    static constexpr uint64_t physics          = (1u << 12);
    static constexpr uint64_t raytrace         = (1u << 13);
    static constexpr uint64_t frame_controller = (1u << 14);
    static constexpr uint64_t grid             = (1u << 15);
    static constexpr uint64_t rendertarget     = (1u << 16);
};

class Item_filter
{
public:
    [[nodiscard]] auto operator()(uint64_t filter_bits) const -> bool;

    uint64_t require_all_bits_set          {0};
    uint64_t require_at_least_one_bit_set  {0};
    uint64_t require_all_bits_clear        {0};
    uint64_t require_at_least_one_bit_clear{0};
};

class Item
    : public std::enable_shared_from_this<Item>
{
public:
    Item();
    explicit Item(const std::string_view name);
    virtual ~Item() noexcept;

    [[nodiscard]] virtual auto get_item_host() const -> Scene_host*;
    [[nodiscard]] virtual auto get_type     () const -> uint64_t;
    [[nodiscard]] virtual auto type_name    () const -> const char*;
    virtual void handle_flag_bits_update(
        const uint64_t old_flag_bits,
        const uint64_t new_flag_bits
    )
    {
        static_cast<void>(old_flag_bits);
        static_cast<void>(new_flag_bits);
    }

    [[nodiscard]] auto get_name           () const -> const std::string&;
    [[nodiscard]] auto get_label          () const -> const std::string&;
    [[nodiscard]] auto get_flag_bits      () const -> uint64_t;
    [[nodiscard]] auto get_id             () const -> erhe::toolkit::Unique_id<Item>::id_type;
    [[nodiscard]] auto is_selected        () const -> bool;
    [[nodiscard]] auto is_visible         () const -> bool;
    [[nodiscard]] auto is_shown_in_ui     () const -> bool;
    [[nodiscard]] auto is_hidden          () const -> bool;
    [[nodiscard]] auto describe           () -> std::string;
    [[nodiscard]] auto get_wireframe_color() const -> glm::vec4;

    void set_name           (const std::string_view name);
    void set_flag_bits      (uint64_t mask, bool value);
    void enable_flag_bits   (uint64_t mask);
    void disable_flag_bits  (uint64_t mask);
    void set_selected       (bool value);
    virtual void set_visible(bool value);
    void show               ();
    void hide               ();
    void set_wireframe_color(const glm::vec4& color);

protected:
    uint64_t                       m_flag_bits      {Item_flags::none};
    glm::vec4                      m_wireframe_color{0.6f, 0.7f, 0.8f, 0.7f};
    erhe::toolkit::Unique_id<Item> m_id;
    std::string                    m_name;
    std::string                    m_label;
};

} // namespace erhe::scene
