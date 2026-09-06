#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe {
    class Typed;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Xformable; using Node = Xformable;
    using Layer_id = uint64_t;
}

namespace erhe::usd {

// One composed USD stage. The USD library that produced it is an
// implementation detail: nothing in this header names a LightUSD type, and
// erhe::usd is the only erhe library that includes LightUSD headers.
class Stage final
{
public:
    class Impl;

    explicit Stage(std::unique_ptr<Impl>&& impl);
    ~Stage() noexcept;

    Stage           (const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage           (Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;

    [[nodiscard]] auto get_source_path() const -> const std::filesystem::path&;
    [[nodiscard]] auto get_impl       () const -> const Impl&;

private:
    std::unique_ptr<Impl> m_impl;
};

// How many prims of one schema type the stage holds. `type_name` is the USD
// schema type name ("Xform", "Mesh", "Material", ...); a prim without a type
// name is counted under an empty string.
class Prim_type_count final
{
public:
    std::string type_name;
    std::size_t count{0};
};

// One layer the stage names. `kind` is "root" for the file that was loaded,
// "sublayer" for a `subLayers` entry of the root layer, and "reference" or
// "payload" for a composition arc a prim authored.
class Layer_reference final
{
public:
    std::string kind;
    std::string asset_path;
};

class Stage_description final
{
public:
    std::size_t                  prim_count{0};
    std::vector<Prim_type_count> prim_types;
    std::vector<Layer_reference> layers;
    std::string                  up_axis;
    std::string                  default_prim;
    double                       meters_per_unit{1.0};
};

// Result of load_stage(). `stage` is null exactly when `error` is non-empty;
// `warning` can be non-empty either way. erhe::usd reports failures as values
// rather than exceptions, the way LightUSD itself does.
class Load_stage_result final
{
public:
    std::unique_ptr<Stage> stage;
    std::string            error;
    std::string            warning;
};

// Load and compose a .usd / .usda / .usdc / .usdz file. The file format is
// detected from its content.
[[nodiscard]] auto load_stage(const std::filesystem::path& path) -> Load_stage_result;

// Summarize a loaded stage: prim count, per-schema-type prim counts sorted by
// type name, and the layers the stage names.
[[nodiscard]] auto describe_stage(const Stage& stage) -> Stage_description;

// One image the stage's materials reference. LightUSD's built-in image
// loaders are off in erhe's build (they duplicate what erhe::graphics
// already decodes), so an image arrives as a resolved file path and the
// caller loads the pixels with erhe's own image loading.
class Usd_image final
{
public:
    std::string           name;
    std::filesystem::path path;
    // The USD color space of the source asset says whether the texels are
    // sRGB-encoded; a normal / occlusion map is raw.
    bool                  srgb{true};
};

// The five texture slots of erhe::primitive::Material_texture_samplers, in
// the same order as the glTF importer's Gltf_material_texture_slot.
enum class Usd_material_texture_slot : unsigned int {
    base_color         = 0,
    metallic_roughness = 1,
    normal             = 2,
    occlusion          = 3,
    emissive           = 4
};

// Which image belongs in which texture slot of which material. erhe::usd
// creates no GPU object, so the slots themselves stay empty and the caller
// fills them once it has created the textures.
class Usd_material_texture_binding final
{
public:
    std::size_t               material_index{0};
    Usd_material_texture_slot slot{Usd_material_texture_slot::base_color};
    std::size_t               image_index{0};
};

// Everything one USD file contributes to a scene, in erhe types - the USD
// counterpart of erhe::gltf::Gltf_data, and deliberately the same shape
// where the two formats overlap. `nodes` holds every imported node (the
// top-level ones parented to Usd_load_arguments::root_node); meshes,
// cameras and lights are attached to the nodes that carry them and are
// listed here as well, the way the glTF importer lists them.
class Usd_data final
{
public:
    std::vector<std::shared_ptr<erhe::scene::Node>>         nodes;
    // The prims of the tree that carry no transform: the `Scope` prims and
    // the `Typed` prims a `typeName` without an erhe class becomes
    // (doc/usd-compatibility-plan.md C5). They are parented into the same
    // tree `nodes` is parented into, so a caller that inserts the tree's
    // root takes them with it; the list is here for the reason `nodes` is -
    // so a caller can walk what one file contributed.
    std::vector<std::shared_ptr<erhe::Typed>>               prims;
    std::vector<std::shared_ptr<erhe::scene::Mesh>>         meshes;
    std::vector<std::shared_ptr<erhe::scene::Camera>>       cameras;
    std::vector<std::shared_ptr<erhe::scene::Light>>        lights;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<Usd_image>                                  images;
    std::vector<Usd_material_texture_binding>               material_texture_bindings;

    // Stage constants the import consumed (see load_usd): the up axis and
    // metersPerUnit are applied to the top-level nodes as a root transform,
    // and reported here for the caller's log / UI.
    std::string up_axis{"Y"};
    double      meters_per_unit{1.0};

    // The root layer's `customLayerData`, string entries only: what an erhe
    // save put there (the editor's scene state) and what another writer left
    // for a reader that understands it. Non-string entries are not reported.
    std::map<std::string, std::string> custom_layer_data;
};

class Usd_load_arguments final
{
public:
    std::filesystem::path                    path;
    std::shared_ptr<erhe::scene::Node>       root_node;
    erhe::scene::Layer_id                    mesh_layer_id{0};
};

// Result of load_usd(). `error` is non-empty exactly when the load failed,
// in which case `data` is empty; `warning` can be non-empty either way.
class Usd_load_result final
{
public:
    Usd_data    data;
    std::string error;
    std::string warning;
};

// Load a USD file (through load_stage) and convert its composed stage into
// erhe scene content: nodes and transforms, meshes, materials, image
// references, cameras and lights. Values are read at the stage's default
// time code; UsdPhysics API schemas are logged once per file and skipped.
[[nodiscard]] auto load_usd(const Usd_load_arguments& arguments) -> Usd_load_result;

// Convert an already loaded stage. load_usd() is this plus load_stage().
[[nodiscard]] auto convert_stage(const Stage& stage, const Usd_load_arguments& arguments) -> Usd_load_result;

// ---------------------------------------------------------------------------
// Export (doc/usd-compatibility-plan.md E1)
// ---------------------------------------------------------------------------

// A USD identifier: the C identifier grammar, no other characters. Every
// character outside [A-Za-z0-9_] becomes '_', and a name that starts with a
// digit gets a leading '_'. An empty name becomes "_". The writer applies
// this to every prim name and then makes the result sibling-unique with the
// same suffix rule erhe uses for item names (M2), so two names that sanitize
// onto one spelling still yield two prims.
[[nodiscard]] auto sanitize_usd_identifier(std::string_view name) -> std::string;

// One image the writer binds into a material's shading network. The caller
// resolves the image to a file: erhe::usd creates no GPU object and decodes
// nothing, so a texture without a source file (a generated one) has no entry
// here and the writer warns for that slot.
class Usd_save_texture final
{
public:
    std::size_t               material_index{0};
    Usd_material_texture_slot slot          {Usd_material_texture_slot::base_color};
    std::filesystem::path     path          {};
    // The USD color space of the source asset; a normal / occlusion map is raw.
    bool                      srgb          {true};
};

// What save_usda() writes. The content is erhe's own - the writer is handed
// the scene it is to write, not a Usd_data - and the stage constants are the
// caller's choice (the defaults are what erhe means: Y up, metres).
class Usd_save_arguments final
{
public:
    std::filesystem::path                                   path;
    // The node whose children are written. The root node itself is not a
    // prim: an erhe item path excludes the root's own name (M1), so the
    // root's children are the stage's top-level prims.
    std::shared_ptr<const erhe::scene::Node>                root_node;
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<Usd_save_texture>                           textures;
    // Written verbatim as the root layer's `customLayerData`, one string
    // entry per pair: how the editor carries its own scene state in a USD
    // file (doc/scene_serialization.md, USD-backed scenes).
    std::map<std::string, std::string>                      custom_layer_data;
    std::string                                             up_axis        {"Y"};
    double                                                  meters_per_unit{1.0};
};

// Result of save_usda(). `error` is non-empty exactly when the save failed;
// `warning` collects everything the writer had to leave out.
class Usd_save_result final
{
public:
    std::string error;
    std::string warning;
};

// Write one `.usda` layer holding the scene under Usd_save_arguments::root_node:
// Xform / Mesh / GeomSubset / Camera / UsdLux prims per doc/usd_compatibility.md,
// a `/Materials` scope holding Material + Shader networks, local property
// values only (D32), erhe-only properties as `erhe:Owner:name` custom
// attributes and item tags as UsdCollectionAPI collections on the default
// prim. Failures are values, not exceptions.
[[nodiscard]] auto save_usda(const Usd_save_arguments& arguments) -> Usd_save_result;

} // namespace erhe::usd
