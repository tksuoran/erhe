// Material texture slots as member-backed object properties
// (doc/erhe/property_system.md D28): the slot member is the value, the
// property notifies, the traits reject a pointee that is not a
// Texture_reference.

#include "erhe_primitive/material.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_property/property_set.hpp"
#include "erhe_property/property_string.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include <memory>

using erhe::primitive::Material;
using erhe::primitive::Material_data;
using namespace erhe::property;

namespace {

// A Texture_reference that is an item but not a GPU texture.
class Fake_texture
    : public erhe::Item<erhe::Item_base, erhe::Item_base, Fake_texture, erhe::Item_kind::not_clonable>
    , public erhe::graphics::Texture_reference
{
public:
    explicit Fake_texture(const std::string_view name) : Item{name} {}
    static constexpr std::string_view static_type_name{"Fake_texture"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::texture; }
    auto get_referenced_texture() const -> const erhe::graphics::Texture* override { return nullptr; }
};

} // anonymous namespace

TEST(Material_textures, property_is_the_value_and_the_member_mirrors_it)
{
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("t");

    EXPECT_FALSE(material->get_value(Material::base_color_texture_property).object);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_property.get()), Value_source::default_value);

    // A property write reaches the member mirror and observers.
    int notifications = 0;
    const Observer_token token = material->add_observer([&notifications](Dependency_object&, const Property_changed_args&) { ++notifications; });
    material->set_normal_texture(texture);
    EXPECT_EQ(material->get_value_source(Material::normal_texture_property.get()), Value_source::local);
    EXPECT_EQ(material->get_data().texture_samplers.normal.texture_reference.get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(material->get_normal_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(to_string(Material::normal_texture_property.get(), material->get_value(Material::normal_texture_property)), "t");
    EXPECT_EQ(notifications, 1);
    material->set_normal_texture(texture); // unchanged: no notification
    EXPECT_EQ(notifications, 1);
    material->set_normal_texture({});
    EXPECT_FALSE(material->get_data().texture_samplers.normal.texture_reference);
    EXPECT_EQ(notifications, 2);

    // A create-info slot fill seeds a local value; an unset slot stays default.
    erhe::primitive::Material_create_info create_info{.name = "c"};
    create_info.data.texture_samplers.emissive.texture_reference = texture;
    create_info.data.texture_samplers.emissive.scale             = glm::vec2{3.0f, 3.0f};
    std::shared_ptr<Material> seeded = std::make_shared<Material>(create_info);
    EXPECT_EQ(seeded->get_value_source(Material::emissive_texture_property.get()),          Value_source::local);
    EXPECT_EQ(seeded->get_value_source(Material::emissive_texture_uv_scale_property.get()), Value_source::local);
    EXPECT_EQ(seeded->get_value_source(Material::base_color_texture_property.get()),        Value_source::default_value);
    EXPECT_EQ(seeded->get_emissive_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
}

namespace {

// A holder of Material properties above a material (a content-library
// folder, doc/erhe/property_system.md D30): the material is its inheritance child.
class Folder : public erhe::property::Dependency_object
{
public:
    std::vector<Material*> materials;
    auto get_secondary_property_owner_type() const -> std::optional<Owner_type> override { return Material::property_owner_type(); }
    void for_each_inheritance_child(const std::function<void(Dependency_object&)>& callback) override
    {
        for (Material* material : materials) {
            callback(*material);
        }
    }
};

} // anonymous namespace

TEST(Material_textures, slot_inherits_from_a_folder_and_mirrors)
{
    Folder                        folder;
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("t");
    folder.materials.push_back(material.get());
    material->set_inheritance_container(&folder);

    folder.set_value(Material::base_color_texture_property, Object_reference{texture});
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_property.get()), Value_source::inherited);
    EXPECT_EQ(material->get_base_color_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    folder.set_value(Material::base_color_texture_uv_scale_property, glm::vec2{2.0f, 2.0f});
    EXPECT_EQ(material->get_data().texture_samplers.base_color.scale, (glm::vec2{2.0f, 2.0f}));

    // A local value shadows the folder; clearing it re-reads the folder.
    material->set_base_color_texture({});
    EXPECT_FALSE(material->get_base_color_texture());
    material->clear_value(Material::base_color_texture_property);
    EXPECT_EQ(material->get_base_color_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    folder.clear_value(Material::base_color_texture_property);
    EXPECT_FALSE(material->get_base_color_texture());
    material->set_inheritance_container(nullptr);
}

TEST(Material_textures, traits_reject_a_material_as_a_texture)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    std::shared_ptr<Material> other    = std::make_shared<Material>("other");
    material->set_value(Material::emissive_texture_property, Object_reference{other});
    EXPECT_FALSE(material->get_data().texture_samplers.emissive.texture_reference);
}

TEST(Material_textures, set_data_writes_slots_through_the_properties)
{
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("t");

    int notifications = 0;
    const Observer_token token = material->add_observer([&notifications](Dependency_object&, const Property_changed_args&) { ++notifications; });

    Material_data after{};
    after.texture_samplers.occlusion.texture_reference = texture;
    after.texture_samplers.occlusion.scale             = glm::vec2{2.0f, 2.0f};
    material->set_data(after);
    EXPECT_EQ(material->get_occlusion_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(material->get_data().texture_samplers.occlusion.scale, (glm::vec2{2.0f, 2.0f}));
    EXPECT_EQ(notifications, 2); // the texture and the UV scale, both properties; the other slot fields are unchanged
    EXPECT_TRUE(material->get_data() == after);

    material->set_data(Material_data{});
    EXPECT_FALSE(material->get_occlusion_texture());
    EXPECT_EQ(notifications, 4);
}

TEST(Material_textures, equality_and_property_set_see_the_slot)
{
    std::shared_ptr<Material>     a       = std::make_shared<Material>("a");
    std::shared_ptr<Material>     b       = std::make_shared<Material>("a");
    std::shared_ptr<Fake_texture> texture = std::make_shared<Fake_texture>("t");
    EXPECT_TRUE(*a == *b);
    a->set_base_color_texture(texture);
    EXPECT_FALSE(*a == *b);

    // Member-backed: listed among the local values, so a copied bag carries it.
    const Property_set bag = Property_set::read_local_values(*a);
    const std::optional<Property_value> entry = bag.find(Material::base_color_texture_property.get());
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(std::get<Object_reference>(entry.value()).object.get(), static_cast<erhe::Item_base*>(texture.get()));
}

// The slot sampler state is seven entry-store properties per slot
// (section 4.1): a property write mirrors into the slot state, a set_data
// snapshot writes the properties (a default field clears the local value),
// and a folder value is inherited.
TEST(Material_textures, sampler_properties_mirror_into_the_slot_state)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    const erhe::primitive::Material_sampler_state defaults{};
    EXPECT_EQ(material->get_data().texture_samplers.base_color.sampler, defaults);

    material->set_value(Material::base_color_texture_wrap_u_property, erhe::graphics::Sampler_address_mode::clamp_to_edge);
    material->set_value(Material::base_color_texture_max_anisotropy_property, 8.0f);
    EXPECT_EQ(material->get_data().texture_samplers.base_color.sampler.wrap_u, erhe::graphics::Sampler_address_mode::clamp_to_edge);
    EXPECT_EQ(material->get_data().texture_samplers.base_color.sampler.max_anisotropy, 8.0f);
    EXPECT_EQ(material->get_data().texture_samplers.base_color.sampler.wrap_v, defaults.wrap_v);
    EXPECT_EQ(material->get_data().texture_samplers.normal.sampler, defaults);

    // set_slot_sampler / set_data: local where the state differs from the
    // default, cleared where it does not.
    erhe::primitive::Material_sampler_state state{};
    state.min_filter  = erhe::graphics::Filter::nearest;
    state.mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped;
    material->set_slot_sampler(material->get_data().texture_samplers.base_color, state);
    EXPECT_EQ(material->get_data().texture_samplers.base_color.sampler, state);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_wrap_u_property.get()),      Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_min_filter_property.get()),  Value_source::local);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_mipmap_mode_property.get()), Value_source::local);

    Material_data snapshot = material->get_data();
    snapshot.texture_samplers.emissive.sampler.lod_bias = -1.5f;
    snapshot.texture_samplers.base_color.sampler        = defaults;
    material->set_data(snapshot);
    EXPECT_EQ(material->get_data().texture_samplers.emissive.sampler.lod_bias, -1.5f);
    EXPECT_EQ(material->get_data().texture_samplers.base_color.sampler, defaults);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_min_filter_property.get()), Value_source::default_value);

    // The create-info conversions round-trip the state.
    EXPECT_EQ(erhe::primitive::sampler_state_from(erhe::primitive::to_sampler_create_info(state)), state);
}

TEST(Material_textures, sampler_state_inherits_from_a_folder)
{
    Folder folder;
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    folder.materials.push_back(material.get());
    material->set_inheritance_container(&folder);

    folder.set_value(Material::normal_texture_wrap_v_property, erhe::graphics::Sampler_address_mode::mirrored_repeat);
    EXPECT_EQ(material->get_value_source(Material::normal_texture_wrap_v_property.get()), Value_source::inherited);
    EXPECT_EQ(material->get_data().texture_samplers.normal.sampler.wrap_v, erhe::graphics::Sampler_address_mode::mirrored_repeat);
    material->set_inheritance_container(nullptr);
}

// Which channel of a slot's texture each scalar input reads. The defaults are
// glTF's fixed packing; USD names the channel in the shading network, so the
// selectors are ordinary material properties (a value, a local set, a folder
// to inherit from).
TEST(Material_textures, texture_channels_default_to_the_gltf_packing)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    EXPECT_EQ(material->get_metallic_channel(),  erhe::primitive::Texture_channel::b);
    EXPECT_EQ(material->get_roughness_channel(), erhe::primitive::Texture_channel::g);
    EXPECT_EQ(material->get_occlusion_channel(), erhe::primitive::Texture_channel::r);
    EXPECT_EQ(material->get_opacity_channel(),   erhe::primitive::Texture_channel::a);
    EXPECT_EQ(material->get_value_source(Material::roughness_channel_property.get()), Value_source::default_value);

    material->set_value(Material::roughness_channel_property, erhe::primitive::Texture_channel::r);
    EXPECT_EQ(material->get_roughness_channel(), erhe::primitive::Texture_channel::r);
    EXPECT_EQ(material->get_value_source(Material::roughness_channel_property.get()), Value_source::local);

    // A whole-values snapshot carries them, and a value equal to the default
    // clears the local one (D32).
    erhe::primitive::Material_values values = material->get_values();
    EXPECT_EQ(values.roughness_channel, erhe::primitive::Texture_channel::r);
    values.roughness_channel = erhe::primitive::Texture_channel::g;
    values.occlusion_channel = erhe::primitive::Texture_channel::a;
    material->set_values(values);
    EXPECT_EQ(material->get_value_source(Material::roughness_channel_property.get()), Value_source::default_value);
    EXPECT_EQ(material->get_occlusion_channel(), erhe::primitive::Texture_channel::a);
}

TEST(Material_textures, texture_channels_inherit_from_a_folder)
{
    Folder folder;
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    folder.materials.push_back(material.get());
    material->set_inheritance_container(&folder);

    folder.set_value(Material::metallic_channel_property, erhe::primitive::Texture_channel::a);
    EXPECT_EQ(material->get_value_source(Material::metallic_channel_property.get()), Value_source::inherited);
    EXPECT_EQ(material->get_metallic_channel(), erhe::primitive::Texture_channel::a);
    material->set_inheritance_container(nullptr);
}

// Where the base color and the fragment alpha come from. A `UsdPreviewSurface`
// input a `UsdPrimvarReader` of `displayColor` / `displayOpacity` feeds is
// what names the mesh's vertex colors; both are ordinary material properties
// (a default, a local set, a folder to inherit from).
TEST(Material_textures, input_sources_default_to_the_materials_own_value)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    EXPECT_EQ(material->get_base_color_source(), erhe::primitive::Material_input_source::value);
    EXPECT_EQ(material->get_opacity_source(),    erhe::primitive::Material_input_source::value);
    EXPECT_EQ(material->get_value_source(Material::base_color_source_property.get()), Value_source::default_value);

    material->set_value(Material::base_color_source_property, erhe::primitive::Material_input_source::vertex_color);
    EXPECT_EQ(material->get_base_color_source(), erhe::primitive::Material_input_source::vertex_color);
    EXPECT_EQ(material->get_value_source(Material::base_color_source_property.get()), Value_source::local);

    erhe::primitive::Material_values values = material->get_values();
    EXPECT_EQ(values.base_color_source, erhe::primitive::Material_input_source::vertex_color);
    values.base_color_source = erhe::primitive::Material_input_source::value;
    values.opacity_source    = erhe::primitive::Material_input_source::vertex_color;
    material->set_values(values);
    EXPECT_EQ(material->get_value_source(Material::base_color_source_property.get()), Value_source::default_value);
    EXPECT_EQ(material->get_opacity_source(), erhe::primitive::Material_input_source::vertex_color);
}

TEST(Material_textures, input_sources_inherit_from_a_folder)
{
    Folder folder;
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    folder.materials.push_back(material.get());
    material->set_inheritance_container(&folder);

    folder.set_value(Material::opacity_source_property, erhe::primitive::Material_input_source::vertex_color);
    EXPECT_EQ(material->get_value_source(Material::opacity_source_property.get()), Value_source::inherited);
    EXPECT_EQ(material->get_opacity_source(), erhe::primitive::Material_input_source::vertex_color);
    material->set_inheritance_container(nullptr);
}

// Detail blend values (doc/plans/detail_blend_weight.md B1.1, B2.1 - B2.4):
// ordinary material properties with the defaults of the requirements, carried
// by the whole-values snapshot and its property bag.
TEST(Material_detail_blend, values_default_to_the_requirements)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    EXPECT_FALSE(material->get_detail_blend());
    EXPECT_EQ(material->get_detail_blend_weight_channel(),   erhe::primitive::Texture_channel::r);
    EXPECT_EQ(material->get_detail_blend_height_channel(),   erhe::primitive::Texture_channel::r);
    EXPECT_EQ(material->get_detail_blend_height_influence(), 1.0f);
    EXPECT_EQ(material->get_detail_blend_transition_width(), 0.2f);
    EXPECT_EQ(material->get_detail_blend_noise_amount(),     0.0f);
    EXPECT_EQ(material->get_detail_blend_noise_scale(),      1.0f);
    EXPECT_EQ(material->get_detail_blend_noise_space(),      erhe::primitive::Detail_blend_noise_space::node);

    const erhe::primitive::Material_values values = material->get_values();
    EXPECT_TRUE(values == erhe::primitive::Material_values{});
    EXPECT_EQ(material->get_value_source(Material::detail_blend_property.get()), Value_source::default_value);
}

TEST(Material_detail_blend, values_round_trip_through_the_snapshot)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    erhe::primitive::Material_values values{};
    values.detail_blend                  = true;
    values.detail_blend_weight_channel   = erhe::primitive::Texture_channel::g;
    values.detail_blend_height_channel   = erhe::primitive::Texture_channel::a;
    values.detail_blend_height_influence = 0.75f;
    values.detail_blend_transition_width = 0.05f;
    values.detail_blend_noise_amount     = 0.25f;
    values.detail_blend_noise_scale      = 4.0f;
    values.detail_blend_noise_space      = erhe::primitive::Detail_blend_noise_space::world;
    material->set_values(values);

    EXPECT_TRUE(material->get_values() == values);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_noise_scale_property.get()), Value_source::local);

    // A snapshot field back at its default clears the local value (D32).
    erhe::primitive::Material_values reset = values;
    reset.detail_blend_noise_scale = 1.0f;
    material->set_values(reset);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_noise_scale_property.get()), Value_source::default_value);

    // The property bag diff carries exactly the changed detail values.
    const Property_set diff = Property_set::diff(Material::to_property_set(reset), Material::to_property_set(values));
    EXPECT_TRUE(diff.find(Material::detail_blend_noise_scale_property.get()).has_value());
    EXPECT_FALSE(diff.find(Material::detail_blend_noise_amount_property.get()).has_value());

    // A copy carries the values.
    const Material copy{*material};
    EXPECT_TRUE(copy.get_values() == reset);
}

TEST(Material_detail_blend, channels_are_one_of_the_four_components)
{
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    // A rejected value leaves the property as it was.
    material->set_value(Material::detail_blend_weight_channel_property, erhe::primitive::Texture_channel::none);
    EXPECT_EQ(material->get_detail_blend_weight_channel(), erhe::primitive::Texture_channel::r);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_weight_channel_property.get()), Value_source::default_value);
    material->set_value(Material::detail_blend_height_channel_property, erhe::primitive::Texture_channel::b);
    EXPECT_EQ(material->get_detail_blend_height_channel(), erhe::primitive::Texture_channel::b);
    material->set_value(Material::detail_blend_noise_scale_property, 0.0f);
    EXPECT_EQ(material->get_detail_blend_noise_scale(), 1.0f);
    material->set_value(Material::detail_blend_transition_width_property, 1.5f);
    EXPECT_EQ(material->get_detail_blend_transition_width(), 0.2f);
}

TEST(Material_detail_blend, values_inherit_from_a_folder)
{
    Folder folder;
    std::shared_ptr<Material> material = std::make_shared<Material>("m");
    folder.materials.push_back(material.get());
    material->set_inheritance_container(&folder);

    folder.set_value(Material::detail_blend_property, true);
    folder.set_value(Material::detail_blend_transition_width_property, 0.5f);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_property.get()), Value_source::inherited);
    EXPECT_TRUE(material->get_detail_blend());
    EXPECT_EQ(material->get_detail_blend_transition_width(), 0.5f);
    material->set_inheritance_container(nullptr);
}

// The sixth slot (B2.2, BD1.3): everything the five slots have - the texture
// reference, the UV transform, the texgen source and the sampler state.
TEST(Material_detail_blend, slot_has_what_the_other_slots_have)
{
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("height");
    const erhe::primitive::Material_texture_sampler& slot = material->get_data().texture_samplers.detail_blend;

    EXPECT_EQ(material->get_slot_texture_property(slot), &Material::detail_blend_texture_property);
    material->set_detail_blend_texture(texture);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_texture_property.get()), Value_source::local);
    EXPECT_EQ(material->get_detail_blend_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(slot.texture_reference.get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));

    material->set_slot_uv_transform(slot, 0.5f, glm::vec2{0.25f, 0.0f}, glm::vec2{4.0f, 4.0f});
    material->set_slot_texgen_mode(slot, erhe::primitive::Texgen_mode::world_xz);
    erhe::primitive::Material_sampler_state state{};
    state.wrap_u = erhe::graphics::Sampler_address_mode::mirrored_repeat;
    material->set_slot_sampler(slot, state);
    EXPECT_EQ(slot.rotation, 0.5f);
    EXPECT_EQ(slot.offset, (glm::vec2{0.25f, 0.0f}));
    EXPECT_EQ(slot.scale, (glm::vec2{4.0f, 4.0f}));
    EXPECT_EQ(slot.texgen_mode, erhe::primitive::Texgen_mode::world_xz);
    EXPECT_EQ(slot.sampler, state);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_texture_uv_scale_property.get()),    Value_source::local);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_texture_texgen_mode_property.get()), Value_source::local);
    EXPECT_EQ(material->get_value_source(Material::detail_blend_texture_wrap_u_property.get()),      Value_source::local);

    // The slot takes part in equality, copies and set_data snapshots.
    std::shared_ptr<Material> other = std::make_shared<Material>("m");
    EXPECT_FALSE(*material == *other);
    other->set_data(material->get_data());
    EXPECT_TRUE(other->get_data() == material->get_data());
    const Material copy{*material};
    EXPECT_TRUE(copy.get_data() == material->get_data());

    material->set_data(Material_data{});
    EXPECT_FALSE(material->get_detail_blend_texture());
    EXPECT_EQ(material->get_value_source(Material::detail_blend_texture_uv_scale_property.get()), Value_source::default_value);
}

TEST(Material_detail_blend, slot_inherits_from_a_folder)
{
    Folder                        folder;
    std::shared_ptr<Material>     material = std::make_shared<Material>("m");
    std::shared_ptr<Fake_texture> texture  = std::make_shared<Fake_texture>("height");
    folder.materials.push_back(material.get());
    material->set_inheritance_container(&folder);

    folder.set_value(Material::detail_blend_texture_property, Object_reference{texture});
    folder.set_value(Material::detail_blend_texture_uv_scale_property, glm::vec2{8.0f, 8.0f});
    EXPECT_EQ(material->get_value_source(Material::detail_blend_texture_property.get()), Value_source::inherited);
    EXPECT_EQ(material->get_detail_blend_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(material->get_data().texture_samplers.detail_blend.scale, (glm::vec2{8.0f, 8.0f}));
    folder.clear_value(Material::detail_blend_texture_property);
    EXPECT_FALSE(material->get_detail_blend_texture());
    material->set_inheritance_container(nullptr);
}
