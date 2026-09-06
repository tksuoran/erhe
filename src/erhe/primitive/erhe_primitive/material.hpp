#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/typed.hpp"
#include "erhe_primitive/enums.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_set.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace erhe::graphics {
    class Sampler_create_info;
    class Texture_reference;
}

namespace erhe::primitive {

// How a slot's texture is sampled: plain data, the values of the slot's
// sampler properties (section 4.1 of doc/property-system.md). The renderer
// resolves a GPU sampler from it (Material_sampler_cache); the defaults are
// those of erhe::graphics::Sampler_create_info, so a slot at its defaults
// samples as it did with no explicit sampler.
class Material_sampler_state
{
public:
    erhe::graphics::Sampler_address_mode wrap_u        {erhe::graphics::Sampler_address_mode::repeat};
    erhe::graphics::Sampler_address_mode wrap_v        {erhe::graphics::Sampler_address_mode::repeat};
    erhe::graphics::Filter               min_filter    {erhe::graphics::Filter::linear};
    erhe::graphics::Filter               mag_filter    {erhe::graphics::Filter::nearest};
    erhe::graphics::Sampler_mipmap_mode  mipmap_mode   {erhe::graphics::Sampler_mipmap_mode::linear};
    float                                max_anisotropy{1.0f};
    float                                lod_bias      {0.0f};

    [[nodiscard]] auto operator==(const Material_sampler_state&) const -> bool = default;
};

[[nodiscard]] auto to_sampler_create_info(const Material_sampler_state& state) -> erhe::graphics::Sampler_create_info;
[[nodiscard]] auto sampler_state_from    (const erhe::graphics::Sampler_create_info& create_info) -> Material_sampler_state;

class Material_texture_sampler
{
public:
    std::shared_ptr<erhe::graphics::Texture_reference> texture_reference{};
    Material_sampler_state                             sampler          {};
    Texgen_mode                                        texgen_mode      {Texgen_mode::uv0};
    float                                              rotation         {0.0f};
    glm::vec2                                          offset           {0.0f, 0.0f};
    glm::vec2                                          scale            {1.0f, 1.0f};
};

[[nodiscard]] auto operator==(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs);
[[nodiscard]] auto operator!=(const Material_texture_sampler& lhs, const Material_texture_sampler& rhs);

class Material_texture_samplers
{
public:
    Material_texture_sampler base_color;
    Material_texture_sampler metallic_roughness;
    Material_texture_sampler normal;
    Material_texture_sampler occlusion;
    Material_texture_sampler emissive;
};

[[nodiscard]] auto operator==(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs);
[[nodiscard]] auto operator!=(const Material_texture_samplers& lhs, const Material_texture_samplers& rhs);

// True when the mode needs framebuffer blending (so the host mesh must
// route through the translucent composition-pass family). alpha_test and
// screen_door render with depth write enabled and discard for masked
// pixels, so they stay in the opaque pass.
[[nodiscard]] inline auto needs_translucent_pass(const Material_blending_mode mode) -> bool
{
    switch (mode) {
        case Material_blending_mode::alpha_blend:
        case Material_blending_mode::multiply:
        case Material_blending_mode::add:
        case Material_blending_mode::subtract:
            return true;
        case Material_blending_mode::opaque:
        case Material_blending_mode::screen_door:
        case Material_blending_mode::alpha_test:
            return false;
    }
    return false;
}

// The material state whose storage is this struct: the texture slots.
// Every slot field is a mirror of a registered property of the Material
// (doc/property-system.md section 4.1): the texture_reference (D28), the
// texgen_mode, rotation, offset and scale, and the sampler state. Write
// them through the Material properties or set_data on a live material.
class Material_data
{
public:
    Material_texture_samplers texture_samplers{};
};

// Plain snapshot of every registered Material property, for code that reads
// or writes the whole set at once (renderer upload, glTF import / export,
// MCP edits). Material::get_values() / set_values() convert; the property
// store is the source of truth.
class Material_values
{
public:
    glm::vec3                 base_color                        {1.0f, 1.0f, 1.0f};
    float                     opacity                           {1.0f};
    glm::vec2                 roughness                         {0.5f, 0.5f};
    float                     metallic                          {0.0f};
    float                     reflectance                       {0.5f};
    glm::vec3                 emissive                          {0.0f, 0.0f, 0.0f};
    float                     ior                               {1.5f};
    float                     transmission                      {0.0f};
    float                     normal_texture_scale              {1.0f};
    // Storage encoding of the bound normal texture (handedness and, for
    // X+Y maps, the channel layout). Authorable in the Properties window;
    // a KTX2 normal-mode texture overrides the channel layout at shader
    // variant derivation (the flag travels on the texture), keeping the
    // authored handedness.
    Normalmap_encoding        normalmap_encoding                {Normalmap_encoding::right_handed_three_channel};
    float                     occlusion_texture_strength        {1.0f};
    Bxdf_model                bxdf_model                        {Bxdf_model::isotropic_brdf};
    Material_blending_mode    blending_mode                     {Material_blending_mode::opaque};
    bool                      double_sided                      {false};
    float                     alpha_cutoff                      {0.5f};
    bool                      use_circular_brushed_metal        {false};
    // Texgen source for the in-shader circular brushed metal block
    // (T/B derivation and isotropy falloff). Defaults to uv1, matching the
    // default target of generate_mesh_facet_texture_coordinates().
    Texgen_mode               circular_brushed_metal_texgen_mode{Texgen_mode::uv1};
    bool                      use_aniso_control                 {false};
};

[[nodiscard]] auto operator==(const Material_values& lhs, const Material_values& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material_values& lhs, const Material_values& rhs) -> bool;

class Material_create_info
{
public:
    std::string     name  {};
    Material_values values{};
    Material_data   data  {};
};

class Material : public erhe::Item<erhe::Item_base, erhe::Typed, Material>
{
public:
    Material();
    explicit Material(const Material&);
    Material& operator=(const Material&);
    ~Material() noexcept override;

    // A full snapshot: every Material_values field that differs from the
    // property's default becomes a local value, a field equal to the default
    // clears it (see set_values).
    explicit Material(const Material_create_info& create_info);
    // No local values: the defaults, or a style (D25), supply every field.
    explicit Material(std::string_view name);

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Material"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Typed::get_static_type() | erhe::Item_type::material; }

    // Overrides erhe::Typed: the class fixes the token, which is USD
    // `UsdShadeMaterial`'s (doc/usd_compatibility.md, "Materials").
    [[nodiscard]] auto get_class_type_name() const -> std::string_view override { return "Material"; }

    // Registered properties (erhe::property, doc/property-system.md
    // section 4.1). The typed accessors below read and write these on the
    // item's property store.
    static const erhe::property::Property<glm::vec3>              base_color_property;
    static const erhe::property::Property<float>                  opacity_property;
    static const erhe::property::Property<glm::vec2>              roughness_property;
    static const erhe::property::Property<float>                  metallic_property;
    static const erhe::property::Property<float>                  reflectance_property;
    static const erhe::property::Property<glm::vec3>              emissive_property;
    static const erhe::property::Property<float>                  ior_property;
    static const erhe::property::Property<float>                  transmission_property;
    static const erhe::property::Property<float>                  normal_texture_scale_property;
    static const erhe::property::Property<Normalmap_encoding>     normalmap_encoding_property;
    static const erhe::property::Property<float>                  occlusion_texture_strength_property;
    static const erhe::property::Property<Bxdf_model>             bxdf_model_property;
    static const erhe::property::Property<Material_blending_mode> blending_mode_property;
    static const erhe::property::Property<bool>                   double_sided_property;
    static const erhe::property::Property<float>                  alpha_cutoff_property;
    static const erhe::property::Property<bool>                   use_circular_brushed_metal_property;
    static const erhe::property::Property<Texgen_mode>            circular_brushed_metal_texgen_mode_property;
    static const erhe::property::Property<bool>                   use_aniso_control_property;
    // The texture slots (entry-store references that inherit, mirrored
    // into data.texture_samplers by on_property_changed): an
    // Object_reference whose pointee is a Texture_reference (a Texture, a
    // Graph_texture, a Rendergraph_node).
    static const erhe::property::Property<erhe::property::Object_reference> base_color_texture_property;
    static const erhe::property::Property<erhe::property::Object_reference> metallic_roughness_texture_property;
    static const erhe::property::Property<erhe::property::Object_reference> normal_texture_property;
    static const erhe::property::Property<erhe::property::Object_reference> occlusion_texture_property;
    static const erhe::property::Property<erhe::property::Object_reference> emissive_texture_property;
    // The slot transforms (member-backed over the same slots): the texgen
    // source and the UV rotation, offset and scale the shader applies.
    static const erhe::property::Property<Texgen_mode> base_color_texture_texgen_mode_property;
    static const erhe::property::Property<float>       base_color_texture_uv_rotation_property;
    static const erhe::property::Property<glm::vec2>   base_color_texture_uv_offset_property;
    static const erhe::property::Property<glm::vec2>   base_color_texture_uv_scale_property;
    static const erhe::property::Property<Texgen_mode> metallic_roughness_texture_texgen_mode_property;
    static const erhe::property::Property<float>       metallic_roughness_texture_uv_rotation_property;
    static const erhe::property::Property<glm::vec2>   metallic_roughness_texture_uv_offset_property;
    static const erhe::property::Property<glm::vec2>   metallic_roughness_texture_uv_scale_property;
    static const erhe::property::Property<Texgen_mode> normal_texture_texgen_mode_property;
    static const erhe::property::Property<float>       normal_texture_uv_rotation_property;
    static const erhe::property::Property<glm::vec2>   normal_texture_uv_offset_property;
    static const erhe::property::Property<glm::vec2>   normal_texture_uv_scale_property;
    static const erhe::property::Property<Texgen_mode> occlusion_texture_texgen_mode_property;
    static const erhe::property::Property<float>       occlusion_texture_uv_rotation_property;
    static const erhe::property::Property<glm::vec2>   occlusion_texture_uv_offset_property;
    static const erhe::property::Property<glm::vec2>   occlusion_texture_uv_scale_property;
    static const erhe::property::Property<Texgen_mode> emissive_texture_texgen_mode_property;
    static const erhe::property::Property<float>       emissive_texture_uv_rotation_property;
    static const erhe::property::Property<glm::vec2>   emissive_texture_uv_offset_property;
    static const erhe::property::Property<glm::vec2>   emissive_texture_uv_scale_property;
    // The slot samplers (entry-store, inherit like the slot texture,
    // mirrored into the slot's Material_sampler_state): wrap, filters,
    // mipmap mode, anisotropy and the LOD bias.
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> base_color_texture_wrap_u_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> base_color_texture_wrap_v_property;
    static const erhe::property::Property<erhe::graphics::Filter>               base_color_texture_min_filter_property;
    static const erhe::property::Property<erhe::graphics::Filter>               base_color_texture_mag_filter_property;
    static const erhe::property::Property<erhe::graphics::Sampler_mipmap_mode>  base_color_texture_mipmap_mode_property;
    static const erhe::property::Property<float>                                base_color_texture_max_anisotropy_property;
    static const erhe::property::Property<float>                                base_color_texture_lod_bias_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> metallic_roughness_texture_wrap_u_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> metallic_roughness_texture_wrap_v_property;
    static const erhe::property::Property<erhe::graphics::Filter>               metallic_roughness_texture_min_filter_property;
    static const erhe::property::Property<erhe::graphics::Filter>               metallic_roughness_texture_mag_filter_property;
    static const erhe::property::Property<erhe::graphics::Sampler_mipmap_mode>  metallic_roughness_texture_mipmap_mode_property;
    static const erhe::property::Property<float>                                metallic_roughness_texture_max_anisotropy_property;
    static const erhe::property::Property<float>                                metallic_roughness_texture_lod_bias_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> normal_texture_wrap_u_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> normal_texture_wrap_v_property;
    static const erhe::property::Property<erhe::graphics::Filter>               normal_texture_min_filter_property;
    static const erhe::property::Property<erhe::graphics::Filter>               normal_texture_mag_filter_property;
    static const erhe::property::Property<erhe::graphics::Sampler_mipmap_mode>  normal_texture_mipmap_mode_property;
    static const erhe::property::Property<float>                                normal_texture_max_anisotropy_property;
    static const erhe::property::Property<float>                                normal_texture_lod_bias_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> occlusion_texture_wrap_u_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> occlusion_texture_wrap_v_property;
    static const erhe::property::Property<erhe::graphics::Filter>               occlusion_texture_min_filter_property;
    static const erhe::property::Property<erhe::graphics::Filter>               occlusion_texture_mag_filter_property;
    static const erhe::property::Property<erhe::graphics::Sampler_mipmap_mode>  occlusion_texture_mipmap_mode_property;
    static const erhe::property::Property<float>                                occlusion_texture_max_anisotropy_property;
    static const erhe::property::Property<float>                                occlusion_texture_lod_bias_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> emissive_texture_wrap_u_property;
    static const erhe::property::Property<erhe::graphics::Sampler_address_mode> emissive_texture_wrap_v_property;
    static const erhe::property::Property<erhe::graphics::Filter>               emissive_texture_min_filter_property;
    static const erhe::property::Property<erhe::graphics::Filter>               emissive_texture_mag_filter_property;
    static const erhe::property::Property<erhe::graphics::Sampler_mipmap_mode>  emissive_texture_mipmap_mode_property;
    static const erhe::property::Property<float>                                emissive_texture_max_anisotropy_property;
    static const erhe::property::Property<float>                                emissive_texture_lod_bias_property;
    // The sampler state of one of this material's own slots, written through
    // the slot's seven sampler properties in one change batch (a field at its
    // default clears the local value, as set_data does).
    void set_slot_sampler(Material_texture_sampler& slot, const Material_sampler_state& state);

    [[nodiscard]] auto get_base_color                        () const -> glm::vec3              { return get_value(base_color_property); }
    [[nodiscard]] auto get_opacity                           () const -> float                  { return get_value(opacity_property); }
    [[nodiscard]] auto get_roughness                         () const -> glm::vec2              { return get_value(roughness_property); }
    [[nodiscard]] auto get_metallic                          () const -> float                  { return get_value(metallic_property); }
    [[nodiscard]] auto get_reflectance                       () const -> float                  { return get_value(reflectance_property); }
    [[nodiscard]] auto get_emissive                          () const -> glm::vec3              { return get_value(emissive_property); }
    [[nodiscard]] auto get_ior                               () const -> float                  { return get_value(ior_property); }
    [[nodiscard]] auto get_transmission                      () const -> float                  { return get_value(transmission_property); }
    [[nodiscard]] auto get_normal_texture_scale              () const -> float                  { return get_value(normal_texture_scale_property); }
    [[nodiscard]] auto get_normalmap_encoding                () const -> Normalmap_encoding     { return get_value(normalmap_encoding_property); }
    [[nodiscard]] auto get_occlusion_texture_strength        () const -> float                  { return get_value(occlusion_texture_strength_property); }
    [[nodiscard]] auto get_bxdf_model                        () const -> Bxdf_model             { return get_value(bxdf_model_property); }
    [[nodiscard]] auto get_blending_mode                     () const -> Material_blending_mode { return get_value(blending_mode_property); }
    [[nodiscard]] auto get_double_sided                      () const -> bool                   { return get_value(double_sided_property); }
    [[nodiscard]] auto get_alpha_cutoff                      () const -> float                  { return get_value(alpha_cutoff_property); }
    [[nodiscard]] auto get_use_circular_brushed_metal        () const -> bool                   { return get_value(use_circular_brushed_metal_property); }
    [[nodiscard]] auto get_circular_brushed_metal_texgen_mode() const -> Texgen_mode            { return get_value(circular_brushed_metal_texgen_mode_property); }
    [[nodiscard]] auto get_use_aniso_control                 () const -> bool                   { return get_value(use_aniso_control_property); }
    [[nodiscard]] auto get_base_color_texture                () const -> const std::shared_ptr<erhe::graphics::Texture_reference>& { return data.texture_samplers.base_color.texture_reference; }
    [[nodiscard]] auto get_metallic_roughness_texture        () const -> const std::shared_ptr<erhe::graphics::Texture_reference>& { return data.texture_samplers.metallic_roughness.texture_reference; }
    [[nodiscard]] auto get_normal_texture                    () const -> const std::shared_ptr<erhe::graphics::Texture_reference>& { return data.texture_samplers.normal.texture_reference; }
    [[nodiscard]] auto get_occlusion_texture                 () const -> const std::shared_ptr<erhe::graphics::Texture_reference>& { return data.texture_samplers.occlusion.texture_reference; }
    [[nodiscard]] auto get_emissive_texture                  () const -> const std::shared_ptr<erhe::graphics::Texture_reference>& { return data.texture_samplers.emissive.texture_reference; }

    void set_base_color                        (const glm::vec3& value)      { set_value(base_color_property, value); }
    void set_opacity                           (float value)                 { set_value(opacity_property, value); }
    void set_roughness                         (const glm::vec2& value)      { set_value(roughness_property, value); }
    void set_metallic                          (float value)                 { set_value(metallic_property, value); }
    void set_reflectance                       (float value)                 { set_value(reflectance_property, value); }
    void set_emissive                          (const glm::vec3& value)      { set_value(emissive_property, value); }
    void set_ior                               (float value)                 { set_value(ior_property, value); }
    void set_transmission                      (float value)                 { set_value(transmission_property, value); }
    void set_normal_texture_scale              (float value)                 { set_value(normal_texture_scale_property, value); }
    void set_normalmap_encoding                (Normalmap_encoding value)    { set_value(normalmap_encoding_property, value); }
    void set_occlusion_texture_strength        (float value)                 { set_value(occlusion_texture_strength_property, value); }
    void set_bxdf_model                        (Bxdf_model value)            { set_value(bxdf_model_property, value); }
    void set_blending_mode                     (Material_blending_mode value){ set_value(blending_mode_property, value); }
    void set_double_sided                      (bool value)                  { set_value(double_sided_property, value); }
    void set_alpha_cutoff                      (float value)                 { set_value(alpha_cutoff_property, value); }
    void set_use_circular_brushed_metal        (bool value)                  { set_value(use_circular_brushed_metal_property, value); }
    void set_circular_brushed_metal_texgen_mode(Texgen_mode value)           { set_value(circular_brushed_metal_texgen_mode_property, value); }
    void set_use_aniso_control                 (bool value)                  { set_value(use_aniso_control_property, value); }
    void set_base_color_texture                (const std::shared_ptr<erhe::graphics::Texture_reference>& texture);
    void set_metallic_roughness_texture        (const std::shared_ptr<erhe::graphics::Texture_reference>& texture);
    void set_normal_texture                    (const std::shared_ptr<erhe::graphics::Texture_reference>& texture);
    void set_occlusion_texture                 (const std::shared_ptr<erhe::graphics::Texture_reference>& texture);
    void set_emissive_texture                  (const std::shared_ptr<erhe::graphics::Texture_reference>& texture);

    // Whole Material_data in: every slot field goes through its property
    // (one change batch; a field at its default clears the local value).
    // The way undo applies a Material_data snapshot.
    void set_data(const Material_data& new_data);
    // The texture of one of this material's own slots (data.texture_samplers.*),
    // for a caller holding the slot by pointer; a slot of another material
    // is rejected with a logged error.
    void set_slot_texture(Material_texture_sampler& slot, const std::shared_ptr<erhe::graphics::Texture_reference>& texture);
    // The property of one of this material's own slots; nullptr for a slot
    // of another material.
    [[nodiscard]] auto get_slot_texture_property(const Material_texture_sampler& slot) const -> const erhe::property::Property<erhe::property::Object_reference>*;

    // Whole-set snapshot in and out of the property store. set_values()
    // writes the fields that differ from the property's default as local
    // values in one change batch and clears the local value of the rest
    // (doc/property-system.md D32: a local value is an authored value).
    [[nodiscard]] auto get_values() const -> Material_values;
    void               set_values(const Material_values& values);

    // The Material_values fields as a property bag (D17), e.g. for
    // Property_set::diff() between two snapshots.
    [[nodiscard]] static auto to_property_set(const Material_values& values) -> erhe::property::Property_set;

    // No GPU slot here. A material's slot is a property of the Material_set
    // that issued it (doc/draw_list_material_set_plan.md D0), not of the
    // material: the same Material is normally at a different slot in every set
    // it belongs to, and a single mutable field here is what made "slot 7"
    // mean different materials in different passes.
    Material_data           data;

protected:
    // Overrides Dependency_object: keeps the Material_data slot mirrors of
    // the slot properties current (a local, inherited or default change).
    void on_property_changed(const erhe::property::Property_changed_args& args) override;

private:
    void seed_slot_values_from_data();
};

[[nodiscard]] auto operator==(const Material_data& lhs, const Material_data& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material_data& lhs, const Material_data& rhs) -> bool;

[[nodiscard]] auto operator==(const Material& lhs, const Material& rhs) -> bool;
[[nodiscard]] auto operator!=(const Material& lhs, const Material& rhs) -> bool;

} // namespace erhe::primitive
