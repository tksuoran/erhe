#pragma once

#include <cstdint>
#include <span>

namespace erhe {

// One application type's registration (Item_type::register_application_types).
class Item_type_info
{
public:
    uint64_t    index{0};       // one of Item_type::application_index(i)
    const char* label{nullptr}; // the C++ class name, as Item_type::c_bit_labels for the library types
};

// Item class bits. A concrete class's static type is the OR of its class
// chain (doc/erhe/item.md "Prim classes"), so is<T>() is a bit-subset test.
// A more specific class takes the LOWER index: the editor's icon set picks
// the icon of the lowest set type bit that has one, so a Scope shows the
// scope icon rather than the icon of the Typed level it also carries.
// That is why the application range, whose classes derive from the library
// levels and never the reverse, is the LOW one: indices 1 to
// application_index_count belong to the application, which names them in a
// class of its own (the editor: Editor_item_types) as application_index(i)
// / application_bit(i) and registers their labels once at startup with
// register_application_types(); the library types follow from
// library_first_index up. Index 0 is "none".
class Item_type
{
public:
    static constexpr uint64_t application_first_index = 1;
    static constexpr uint64_t application_index_count = 31;
    static constexpr uint64_t library_first_index     = application_first_index + application_index_count; // 32
    static constexpr uint64_t index_count             = 64; // bit positions 0..63
    [[nodiscard]] static constexpr auto application_index(const unsigned int index) -> uint64_t
    {
        return application_first_index + index;
    }
    [[nodiscard]] static constexpr auto application_bit(const unsigned int index) -> uint64_t
    {
        return uint64_t{1} << application_index(index);
    }
    static constexpr uint64_t application_mask = (uint64_t{1} << library_first_index) - 2u; // bits 1..31

    static constexpr uint64_t index_animation              = library_first_index +  0;
    static constexpr uint64_t index_camera                 = library_first_index +  1;
    static constexpr uint64_t index_light                  = library_first_index +  2;
    static constexpr uint64_t index_material               = library_first_index +  3;
    static constexpr uint64_t index_mesh                   = library_first_index +  4;
    static constexpr uint64_t index_scene                  = library_first_index +  5;
    static constexpr uint64_t index_skin                   = library_first_index +  6;
    static constexpr uint64_t index_texture                = library_first_index +  7;
    // A capability, not a class: the item implements
    // erhe::graphics::Texture_reference (a Texture, or an application class
    // that resolves to one), so it can be the value of a texture slot
    // (erhe::primitive::Material's texture properties).
    static constexpr uint64_t index_texture_reference      = library_first_index +  8;
    // A capability, not a class: the item can be the style source of other
    // objects (erhe::property::Dependency_object::set_style; its secondary
    // property owner type names the class it applies to), so it is what
    // Item_base::style_property accepts.
    static constexpr uint64_t index_style_source           = library_first_index +  9;
    static constexpr uint64_t index_xformable              = library_first_index + 10;
    static constexpr uint64_t index_graph_node             = library_first_index + 11;
    static constexpr uint64_t index_rendergraph_node       = library_first_index + 12;
    static constexpr uint64_t index_physics_material       = library_first_index + 13;
    static constexpr uint64_t index_collision_filter       = library_first_index + 14;
    static constexpr uint64_t index_physics_joint_settings = library_first_index + 15;
    static constexpr uint64_t index_scope                  = library_first_index + 16;
    static constexpr uint64_t index_typed                  = library_first_index + 17;
    static constexpr uint64_t index_imageable              = library_first_index + 18;
    static constexpr uint64_t index_xform                  = library_first_index + 19;
    static constexpr uint64_t index_boundable              = library_first_index + 20;
    static constexpr uint64_t index_gprim                  = library_first_index + 21;
    static constexpr uint64_t index_point_instancer        = library_first_index + 22;
    // One past the last library index (c_bit_labels has library_count entries).
    static constexpr uint64_t library_end_index            = library_first_index + 23;
    static constexpr uint64_t library_count                = library_end_index - library_first_index;

    static constexpr uint64_t none                   =  uint64_t{0};
    static constexpr uint64_t animation              = (uint64_t{1} << index_animation             );
    static constexpr uint64_t camera                 = (uint64_t{1} << index_camera                );
    static constexpr uint64_t light                  = (uint64_t{1} << index_light                 );
    static constexpr uint64_t material               = (uint64_t{1} << index_material              );
    static constexpr uint64_t mesh                   = (uint64_t{1} << index_mesh                  );
    static constexpr uint64_t scene                  = (uint64_t{1} << index_scene                 );
    static constexpr uint64_t skin                   = (uint64_t{1} << index_skin                  );
    static constexpr uint64_t texture                = (uint64_t{1} << index_texture               );
    static constexpr uint64_t texture_reference      = (uint64_t{1} << index_texture_reference     );
    static constexpr uint64_t style_source           = (uint64_t{1} << index_style_source          );
    static constexpr uint64_t xformable              = (uint64_t{1} << index_xformable             );
    static constexpr uint64_t graph_node             = (uint64_t{1} << index_graph_node            );
    static constexpr uint64_t rendergraph_node       = (uint64_t{1} << index_rendergraph_node      );
    static constexpr uint64_t physics_material       = (uint64_t{1} << index_physics_material      );
    static constexpr uint64_t collision_filter       = (uint64_t{1} << index_collision_filter      );
    static constexpr uint64_t physics_joint_settings = (uint64_t{1} << index_physics_joint_settings);
    static constexpr uint64_t scope                  = (uint64_t{1} << index_scope                 );
    static constexpr uint64_t typed                  = (uint64_t{1} << index_typed                 );
    static constexpr uint64_t imageable              = (uint64_t{1} << index_imageable             );
    static constexpr uint64_t xform                  = (uint64_t{1} << index_xform                 );
    static constexpr uint64_t boundable              = (uint64_t{1} << index_boundable             );
    static constexpr uint64_t gprim                  = (uint64_t{1} << index_gprim                 );
    static constexpr uint64_t point_instancer        = (uint64_t{1} << index_point_instancer       );

    // NOTE: The names here must match the C++ class names (the editor names
    // its drag and drop payloads by them); indexed by index - library_first_index.
    static constexpr const char* c_bit_labels[] = {
        "Animation",
        "Camera",
        "Light",
        "Material",
        "Mesh",
        "Scene",
        "Skin",
        "Texture",
        "Texture_reference",
        "Style_source",
        "Xformable",
        "Graph_node",
        "Rendergraph_node",
        "Physics_material",
        "Collision_filter",
        "Physics_joint_settings",
        "Scope",
        "Typed",
        "Imageable",
        "Xform",
        "Boundable",
        "Gprim",
        "Point_instancer"
    };

    // Registers the application types' labels. Every index must be in the
    // application range and listed once; the span must stay alive for the
    // process. Registering again replaces the previous set.
    static void register_application_types(std::span<const Item_type_info> types);
    [[nodiscard]] static auto get_application_types() -> std::span<const Item_type_info>;
    // The label of one type index: a library label, a registered application
    // label, or nullptr for an index neither defines (index 0 included).
    [[nodiscard]] static auto label(uint64_t index) -> const char*;
};

} // namespace erhe
