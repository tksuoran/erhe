// Material texture slots as member-backed object properties
// (doc/property-system.md D28): the slot member is the value, the
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
    EXPECT_EQ(material->data.texture_samplers.normal.texture_reference.get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(material->get_normal_texture().get(), static_cast<erhe::graphics::Texture_reference*>(texture.get()));
    EXPECT_EQ(to_string(Material::normal_texture_property.get(), material->get_value(Material::normal_texture_property)), "t");
    EXPECT_EQ(notifications, 1);
    material->set_normal_texture(texture); // unchanged: no notification
    EXPECT_EQ(notifications, 1);
    material->set_normal_texture({});
    EXPECT_FALSE(material->data.texture_samplers.normal.texture_reference);
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
// folder, doc/property-system.md D30): the material is its inheritance child.
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
    EXPECT_EQ(material->data.texture_samplers.base_color.scale, (glm::vec2{2.0f, 2.0f}));

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
    EXPECT_FALSE(material->data.texture_samplers.emissive.texture_reference);
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
    EXPECT_EQ(material->data.texture_samplers.occlusion.scale, (glm::vec2{2.0f, 2.0f}));
    EXPECT_EQ(notifications, 2); // the texture and the UV scale, both properties; the other slot fields are unchanged
    EXPECT_TRUE(material->data == after);

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
    EXPECT_EQ(material->data.texture_samplers.base_color.sampler, defaults);

    material->set_value(Material::base_color_texture_wrap_u_property, erhe::graphics::Sampler_address_mode::clamp_to_edge);
    material->set_value(Material::base_color_texture_max_anisotropy_property, 8.0f);
    EXPECT_EQ(material->data.texture_samplers.base_color.sampler.wrap_u, erhe::graphics::Sampler_address_mode::clamp_to_edge);
    EXPECT_EQ(material->data.texture_samplers.base_color.sampler.max_anisotropy, 8.0f);
    EXPECT_EQ(material->data.texture_samplers.base_color.sampler.wrap_v, defaults.wrap_v);
    EXPECT_EQ(material->data.texture_samplers.normal.sampler, defaults);

    // set_slot_sampler / set_data: local where the state differs from the
    // default, cleared where it does not.
    erhe::primitive::Material_sampler_state state{};
    state.min_filter  = erhe::graphics::Filter::nearest;
    state.mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped;
    material->set_slot_sampler(material->data.texture_samplers.base_color, state);
    EXPECT_EQ(material->data.texture_samplers.base_color.sampler, state);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_wrap_u_property.get()),      Value_source::default_value);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_min_filter_property.get()),  Value_source::local);
    EXPECT_EQ(material->get_value_source(Material::base_color_texture_mipmap_mode_property.get()), Value_source::local);

    Material_data snapshot = material->data;
    snapshot.texture_samplers.emissive.sampler.lod_bias = -1.5f;
    snapshot.texture_samplers.base_color.sampler        = defaults;
    material->set_data(snapshot);
    EXPECT_EQ(material->data.texture_samplers.emissive.sampler.lod_bias, -1.5f);
    EXPECT_EQ(material->data.texture_samplers.base_color.sampler, defaults);
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
    EXPECT_EQ(material->data.texture_samplers.normal.sampler.wrap_v, erhe::graphics::Sampler_address_mode::mirrored_repeat);
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
