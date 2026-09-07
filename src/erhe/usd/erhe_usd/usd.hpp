#pragma once

#include "erhe_scene/instance_override.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe {
    class Item_base;
    class Typed;
}
namespace erhe::geometry {
    class Geometry;
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

// Which composition arc a Usd_reference came from. A payload is read as a
// reference: erhe loads every arc when the stage is read and has no deferred
// loading (doc/usd-compatibility-plan.md section 5).
enum class Usd_reference_kind : unsigned int {
    reference = 0,
    payload   = 1
};

// One `references` or `payload` arc a prim authors. An empty `asset_path` is
// an internal reference - a prim of the same layer - and an empty `prim_path`
// names the target layer's default prim.
class Usd_reference final
{
public:
    std::string        asset_path;
    std::string        prim_path;
    Usd_reference_kind kind{Usd_reference_kind::reference};
};

// The composition arcs one prim authors, and the erhe prim they were authored
// on: the prim is the carrier of the instances the arcs name, in the order the
// prim's list-edited `references` and `payload` ops resolve to
// (doc/usd-compatibility-plan.md X1).
class Usd_prim_references final
{
public:
    std::shared_ptr<erhe::Item_base> item;
    std::string                      stage_path;
    std::vector<Usd_reference>       references;
    // The `over` prims the referencing layer authors below this prim: the
    // sparse overrides of the instances the arcs bring in
    // (doc/usd-compatibility-plan.md X2). The reader records them and applies
    // nothing - the instance content does not exist until the caller attaches
    // the arcs' targets.
    std::vector<erhe::scene::Instance_override> overrides;
};

// One `class` prim of the root layer (doc/usd-compatibility-plan.md X3). A
// class prim defines no scene content: it holds the opinions its `inherits`
// arcs hand to the prims that name it, which is what an erhe style holds
// (doc/style-library.md D25). erhe::usd records what the layer authored and
// creates no item - the class prim becomes an editor Style item.
class Usd_class_prim final
{
public:
    std::string                                       stage_path;
    std::string                                       name;
    // The `inherits` targets as absolute prim paths, in the order USD
    // composes the list-edited ops into.
    std::vector<std::string>                          inherits;
    // The prim's authored opinions in the neutral name / text form: the
    // qualified `Owner.name` of an `erhe:Owner:name` custom attribute and the
    // bare `visible`, `purpose` and `active` of the native ones.
    std::vector<erhe::scene::Instance_override_value> values;
    // The prims the class holds. Every descendant of a class prim is itself
    // a class, whatever specifier it spells, so a class holding classes is a
    // scope of styles.
    std::vector<Usd_class_prim>                       children;
};

// One prototype held abstract by a `class` prim (doc/usd-compatibility-plan.md
// X3): a `def` descendant of a class prim. USD's class abstraction is what
// keeps such a prim out of the render, so it is imported as an ordinary prim
// with `Item_flags::content` clear, and a reference that names it clones it
// into the referencing prim as content. The item is parented where the class
// prim's own holder is - the class prim itself becomes a Style item, which is
// the caller's to make, and the caller moves the prototype under it.
class Usd_class_prototype final
{
public:
    std::shared_ptr<erhe::Item_base> item;
    std::string                      stage_path;
    // The absolute stage path of the `class` prim holding this prototype.
    std::string                      class_path;
};

// The `inherits` arcs one imported prim authors (X3), and the erhe item the
// prim became. The first target that names a class prim becomes that item's
// style.
class Usd_prim_inherits final
{
public:
    std::shared_ptr<erhe::Item_base> item;
    std::string                      stage_path;
    std::vector<std::string>         inherits;
};

// One material binding a variant authors (doc/usd-compatibility-plan.md X4).
// `relative_path` is the M1 path of the bound prim below the prim carrying
// the variant set, and is empty when the binding is on that prim itself;
// `material_path` is the absolute stage path of the `Material` prim the
// binding names. The reader resolves neither to an item: the paths are what
// the caller re-resolves when the selection changes.
class Usd_variant_binding final
{
public:
    std::string relative_path;
    std::string material_path;
};

// One variant of a variant set: its name and the material bindings it
// authors. Only material bindings are read in this slice - a variant that
// authors anything else has that counted and reported once for the set.
class Usd_variant final
{
public:
    std::string                      name;
    std::vector<Usd_variant_binding> bindings;
};

// One `variantSet` a prim of the stage authors, and the erhe item that prim
// became. `selected` is the layer's `variants` selection for the set, or the
// first variant when the layer authors none - LightUSD composes nothing, so
// the reader is what applies the selected variant's bindings to the imported
// result.
class Usd_variant_set final
{
public:
    std::shared_ptr<erhe::Item_base> prim;
    std::string                      stage_path;
    std::string                      set_name;
    std::vector<Usd_variant>         variants;
    std::string                      selected;
    // How many opinions of the set this slice does not carry: an attribute or
    // a relationship that is not a `material:binding`. Reported once for the
    // set, because a node-subtree variant is the later slice.
    std::size_t                      unsupported_opinion_count{0};
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

// Load a .usd / .usda / .usdc / .usdz file. The file format is detected from
// its content. Composition arcs are not composed away: a referencing prim
// arrives as it was authored, and the arcs it names are read from its metadata
// (doc/usd-compatibility-plan.md X1).
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
    // The image file's bytes when it is packed inside the `.usdz` the stage
    // was loaded from; empty when the image is a loose file, which `path`
    // then names. A packed image has no file of its own, so `path` is where
    // the archive-relative name would sit beside the archive and nothing
    // opens it: the bytes are the only source.
    std::vector<std::uint8_t> bytes;
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

// One `Brush`-typed prim of the root layer (doc/usd-compatibility-plan.md
// E4a). A brush is editor state a USD file carries as a prim of its own type:
// the geometry is the prim's child `Mesh`, and the density, the normal style
// and the material a placed instance gets are its own attributes. erhe::usd
// records what the layer authored and creates no item - the caller makes the
// editor Brush, as it does for a class prim (X3).
class Usd_brush_prim final
{
public:
    std::string                               stage_path;
    std::string                               name;
    // The `def Mesh "geometry"` child, converted the way every other mesh of
    // the file is. Null when the prim has no such child, which is one warning
    // and no brush.
    std::shared_ptr<erhe::geometry::Geometry> geometry;
    float                                     density{1.0f};
    // The `erhe:Brush:normal_style` token, spelled as the glTF field is; empty
    // when the prim authors none.
    std::string                               normal_style;
    // The absolute stage path of the `Material` prim `material:binding` names,
    // empty when the prim binds none.
    std::string                               material_path;
    // Every other authored opinion of the prim in the neutral name / text
    // form, the way a class prim carries its own (X2, X3).
    std::vector<erhe::scene::Instance_override_value> values;
};

// One `DomeLight` prim the file authors. erhe has no environment map, so a
// dome is imported as the scene's ambient light
// (`color * intensity * 2^exposure`, see Usd_data::ambient_light) and the
// prim is recorded here so a save spells it back (src/erhe/usd/notes.md).
class Usd_dome_light final
{
public:
    std::string name;
    // Absolute stage path of the prim.
    std::string stage_path;
    glm::vec3   color    {1.0f, 1.0f, 1.0f};
    float       intensity{1.0f};
    float       exposure {0.0f};
    // `inputs:texture:file` as the file spells it, empty when the dome
    // authors none. The image is not sampled - erhe has no environment map -
    // and the load names it in a warning.
    std::string texture_file;
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
    // The materials the file's `Material` prims became. Each is parented
    // where its prim sits on the stage (doc/usd-compatibility-plan.md U4), so
    // a caller that inserts the tree's root takes them with it; a material
    // the conversion found no prim for has no parent, and it is the caller
    // that decides where such a material goes.
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<Usd_image>                                  images;
    std::vector<Usd_material_texture_binding>               material_texture_bindings;
    // The composition arcs the file's prims author, one entry per prim that
    // authors at least one, in the order the prims were visited
    // (doc/usd-compatibility-plan.md X1). The prims a referencing prim's arcs
    // name are NOT in the lists above: the caller instantiates each arc's
    // target under the carrier.
    std::vector<Usd_prim_references>                        references;
    // The `class` prims the root layer authors, the top-level ones sorted by
    // name and every nested one in the order the layer spells them
    // (doc/usd-compatibility-plan.md X3). A class prim is never a prim of the
    // lists above: Tydra's render-scene conversion never walks one, and the
    // caller turns each into a Style item at the path the class prim has.
    std::vector<Usd_class_prim>                             classes;
    // The `def` descendants of the file's class prims, in the order they were
    // converted (doc/usd-compatibility-plan.md X3). Each is an ordinary prim
    // of the lists above with `Item_flags::content` clear.
    std::vector<Usd_class_prototype>                        class_prototypes;
    // The `inherits` arcs the file's prims author, one entry per prim that
    // authors at least one, in the order the prims were visited.
    std::vector<Usd_prim_inherits>                          prim_inherits;
    // The `variantSet`s the file's prims author, one entry per set, in the
    // order the prims were visited and by set name within one prim
    // (doc/usd-compatibility-plan.md X4). The selected variant's material
    // bindings are already applied to the meshes above; the entry is what
    // lets the caller offer the other selections.
    std::vector<Usd_variant_set>                            variant_sets;
    // The `Brush` prims the root layer authors, in the order the layer spells
    // them (doc/usd-compatibility-plan.md E4a). A brush prim is never a prim
    // of the lists above: the conversion stops at it, so its child mesh is no
    // scene content, and the caller makes one Brush item per record at the
    // path the prim has.
    std::vector<Usd_brush_prim>                             brushes;

    // Stage constants the import consumed (see load_usd): the up axis and
    // metersPerUnit are applied to the top-level nodes as a root transform,
    // and reported here for the caller's log / UI.
    std::string up_axis{"Y"};
    double      meters_per_unit{1.0};
    // The root layer's `defaultPrim`, empty when the file names none: the prim
    // a reference without a prim path targets.
    std::string default_prim;
    // The asset paths of the root layer's `subLayers`, in the order the layer
    // spells them (strongest first). Their content is part of the composed
    // stage this load converted, so the list is what a caller needs to say
    // where the content came from - a save writes the composed content into
    // one layer (src/erhe/usd/notes.md).
    std::vector<std::string> sublayers;

    // The `DomeLight` prims the file authors, in the order the conversion
    // visited them. A dome is never a light of `lights`: erhe has no
    // environment map and the dome becomes ambient light.
    std::vector<Usd_dome_light> dome_lights;
    // The ambient light the first dome of `dome_lights` composes to
    // (`color * intensity * 2^exposure`), black when the file authors none.
    glm::vec3 ambient_light{0.0f, 0.0f, 0.0f};

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

// How the prim an item is written as carries a property value. A prim of a
// USD schema - a Material, a Camera, a UsdLux light - spells the fields its
// schema owns natively, while a typeless prim (an `over` of
// doc/usd-compatibility-plan.md X2, a `class` of X3) has no schema at all, so
// every value of it travels as an `erhe:Owner:name` custom attribute.
enum class Native_property_form : unsigned int {
    schema_attributes = 0,
    custom_attributes = 1
};

// The name the writer authors the property `owner`.`name` under on such a
// prim: the USD schema's own spelling where the prim carries the value
// natively, and `erhe:<owner>:<name>` everywhere else. The Properties
// window's composition-provenance line (doc/usd-compatibility-plan.md X5)
// reads it, so the writer's naming rule is stated once, here. A bridged
// property - the transform, the item name, the tags - is not asked: it
// travels in the USD form that owns it, which the caller names.
[[nodiscard]] auto get_usd_authored_as(std::string_view owner, std::string_view name, Native_property_form form) -> std::string;

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

// One composition arc the writer authors on a prim. `source_path` names the
// file the arc targets as a path of the local file system; the writer writes
// it relative to the file it is saving, and writes no asset path at all when
// the two are the same file - the USD spelling of an internal reference. An
// empty `prim_path` names the target layer's default prim.
class Usd_save_reference final
{
public:
    std::filesystem::path source_path;
    std::string           prim_path;
    Usd_reference_kind    kind{Usd_reference_kind::reference};
};

// The composition arcs one prim of the scene carries. The prim is written as
// the referencing prim it is - its own class, name, transform and authored
// values, plus the `references` and `payload` list ops these arcs give it -
// and the prims below it are not written, because the arcs' targets supply
// them (doc/usd-compatibility-plan.md X1). The caller names the arcs: the
// editor fills them from the carrier's Prefab_instance attachments, and
// nothing in erhe::usd knows that type.
class Usd_save_prim_references final
{
public:
    std::shared_ptr<const erhe::Item_base> item;
    std::vector<Usd_save_reference>        references;
};

// One material binding of one variant the writer authors
// (doc/usd-compatibility-plan.md X4). `relative_path` is the path of the
// bound prim below the prim carrying the set, empty for that prim itself, and
// `material` is bound by the path the writer gives that material's prim.
class Usd_save_variant_binding final
{
public:
    std::string                                      relative_path;
    std::shared_ptr<const erhe::primitive::Material> material;
};

class Usd_save_variant final
{
public:
    std::string                           name;
    std::vector<Usd_save_variant_binding> bindings;
};

// One `variantSet` the writer authors on a prim: the `variantSets` list op,
// the `variants` selection and the variant blocks, whose bindings become
// `over` prims at the relative paths holding a `material:binding`
// relationship. The prim's plain binding stays what the writer writes for the
// material bound today, which the selected variant's bindings equal.
class Usd_save_variant_set final
{
public:
    std::shared_ptr<const erhe::Item_base> item;
    std::string                            set_name;
    std::vector<Usd_save_variant>          variants;
    std::string                            selected;
};

// One brush of the scene the writer authors as a `Brush` prim
// (doc/usd-compatibility-plan.md E4a). erhe::usd names no editor type, so the
// caller hands over what the brush holds: `item` is the brush prim itself,
// whose place in the tree decides where the prim goes, and the rest is what
// the prim carries. A brush prim of the tree the caller does not list here is
// written without geometry, and named in a warning.
class Usd_save_brush final
{
public:
    std::shared_ptr<const erhe::Item_base>           item;
    std::shared_ptr<const erhe::geometry::Geometry>  geometry;
    float                                            density{1.0f};
    // The `erhe:Brush:normal_style` token, spelled as the glTF field is.
    std::string                                      normal_style;
    std::shared_ptr<const erhe::primitive::Material> material;
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
    // The scene's own materials, in the order `textures` indexes them. Where
    // a material prim goes on the stage is decided by the tree - a material
    // is a prim of it (doc/usd-compatibility-plan.md U4) - so this list is
    // the texture index alone: a material listed here but absent from the
    // tree is not written, and a material of the tree that is not listed is
    // written without textures.
    std::vector<std::shared_ptr<erhe::primitive::Material>> materials;
    std::vector<Usd_save_texture>                           textures;
    // The prims that carry composition arcs, one entry per carrier prim. A
    // carrier is written with its arcs and nothing below it; a child of a
    // carrier that instantiation did not seal is a prim the user parented
    // there, and the writer names it in a warning and leaves it out (X1
    // protects the structure inside a reference; X2 decides what such a prim
    // becomes).
    std::vector<Usd_save_prim_references>                   references;
    // The variant sets the scene's prims carry, one entry per set. One prim
    // can carry more than one, and a set is written on the prim its `item`
    // names.
    std::vector<Usd_save_variant_set>                       variant_sets;
    // The brushes the scene's tree holds, one entry per brush prim.
    std::vector<Usd_save_brush>                             brushes;
    // Written verbatim as the root layer's `customLayerData`, one string
    // entry per pair: how the editor carries its own scene state in a USD
    // file (doc/scene_serialization.md, USD-backed scenes).
    std::map<std::string, std::string>                      custom_layer_data;
    // The `DomeLight` prims to write, one top-level prim each: the domes a
    // load recorded (Usd_data::dome_lights). The scene's own ambient light is
    // carried by the `customLayerData` scene block, not by a dome, so a scene
    // that never read one writes none.
    std::vector<Usd_dome_light>                             dome_lights;
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
// a `Material` prim with its Shader network where the erhe material sits in
// the tree, local property values only (D32), erhe-only properties as
// `erhe:Owner:name` custom attributes and item tags as UsdCollectionAPI
// collections on the default prim. A prim named in
// Usd_save_arguments::references is written as a referencing prim - its arcs
// as `references` and `payload` list ops, and nothing below it. Failures are
// values, not exceptions.
[[nodiscard]] auto save_usda(const Usd_save_arguments& arguments) -> Usd_save_result;

} // namespace erhe::usd
