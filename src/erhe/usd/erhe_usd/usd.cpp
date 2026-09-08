#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_item/hierarchy.hpp"
#include "erhe_profile/profile.hpp"

// LightUSD headers. Together with usd_import.cpp this is the only place in
// erhe that includes them; everything the rest of erhe sees is declared in
// usd.hpp.
#include "lightusd.hh"
#include "asset-resolution.hh"
#include "composition.hh"
#include "core/prim.hh"
#include "core/prim-metas.hh"
#include "layer.hh"
#include "stage.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace erhe::usd {

auto flip_texcoord_v(const glm::vec2& uv) -> glm::vec2
{
    return glm::vec2{uv.x, 1.0f - uv.y};
}

auto to_erhe_uv_transform(const Usd_uv_transform_2d& usd) -> Erhe_uv_transform
{
    const float rotation = glm::radians(usd.rotation_degrees);
    Erhe_uv_transform result{};
    result.rotation = -rotation;
    result.scale    = usd.scale;
    result.offset   = glm::vec2{
        usd.translation.x - (std::sin(rotation) * usd.scale.y),
        1.0f - usd.translation.y - (std::cos(rotation) * usd.scale.y)
    };
    return result;
}

auto to_usd_uv_transform_2d(const Erhe_uv_transform& erhe) -> Usd_uv_transform_2d
{
    const float rotation = -erhe.rotation;
    Usd_uv_transform_2d result{};
    result.rotation_degrees = glm::degrees(rotation);
    result.scale            = erhe.scale;
    result.translation      = glm::vec2{
        erhe.offset.x + (std::sin(rotation) * erhe.scale.y),
        1.0f - erhe.offset.y - (std::cos(rotation) * erhe.scale.y)
    };
    return result;
}

Stage::Stage(std::unique_ptr<Impl>&& impl)
    : m_impl{std::move(impl)}
{
}

Stage::~Stage() noexcept = default;

auto Stage::get_source_path() const -> const std::filesystem::path&
{
    return m_impl->source_path;
}

auto Stage::get_impl() const -> const Impl&
{
    return *m_impl.get();
}

namespace {

// How deep a chain of `subLayers` a load follows. A file that nests deeper
// than this is composed down to here and the rest is left out.
constexpr std::size_t c_max_sublayer_depth = 64;

// Asset resolution for the sublayer walk. LightUSD's built-in file resolver
// refuses any asset path holding a `..` segment (io::FindFile), and a sublayer
// stack that keeps its shared layers beside the tree reaches them with exactly
// that (usd-wg test_assets/foundation/stage_configuration/*, whose stubs
// sublayer `../../../_common/axis.usda`). erhe opens local files with the file
// system's own rules - the reach a `references` asset path already has, since
// erhe resolves those itself - so the resolver used here is given the handler
// below, which anchors the path against each search path and normalizes it.
[[nodiscard]] auto resolve_asset_file(
    const char*                     asset_name,
    const std::vector<std::string>& search_paths
) -> std::filesystem::path
{
    std::error_code             error_code{};
    const std::filesystem::path asset_path{asset_name};
    if (asset_path.is_absolute()) {
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(asset_path, error_code);
        const std::filesystem::path candidate  = error_code ? asset_path.lexically_normal() : normalized;
        if (std::filesystem::exists(candidate, error_code)) {
            return candidate;
        }
        return {};
    }
    for (const std::string& search_path : search_paths) {
        error_code.clear();
        const std::filesystem::path joined     = std::filesystem::path{search_path} / asset_path;
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(joined, error_code);
        const std::filesystem::path candidate  = error_code ? joined.lexically_normal() : normalized;
        if (std::filesystem::exists(candidate, error_code)) {
            return candidate;
        }
    }
    return {};
}

int resolve_asset_handler(
    const char*                     asset_name,
    const std::vector<std::string>& search_paths,
    std::string*                    resolved_asset_name,
    std::string*                    err,
    void*                           userdata
)
{
    static_cast<void>(err);
    static_cast<void>(userdata);
    if ((asset_name == nullptr) || (resolved_asset_name == nullptr)) {
        return -2;
    }
    const std::filesystem::path resolved = resolve_asset_file(asset_name, search_paths);
    if (resolved.empty()) {
        return -1;
    }
    *resolved_asset_name = resolved.generic_string();
    return 0;
}

int asset_size_handler(
    const char*    resolved_asset_name,
    std::uint64_t* nbytes,
    std::string*   err,
    void*          userdata
)
{
    static_cast<void>(err);
    static_cast<void>(userdata);
    if ((resolved_asset_name == nullptr) || (nbytes == nullptr)) {
        return -2;
    }
    std::error_code      error_code{};
    const std::uintmax_t size = std::filesystem::file_size(std::filesystem::path{resolved_asset_name}, error_code);
    if (error_code) {
        return -1;
    }
    *nbytes = static_cast<std::uint64_t>(size);
    return 0;
}

int read_asset_handler(
    const char*    resolved_asset_name,
    std::uint64_t  req_nbytes,
    std::uint8_t*  out_buf,
    std::uint64_t* nbytes,
    std::string*   err,
    void*          userdata
)
{
    static_cast<void>(err);
    static_cast<void>(userdata);
    if ((resolved_asset_name == nullptr) || (out_buf == nullptr) || (nbytes == nullptr)) {
        return -2;
    }
    std::ifstream stream{std::filesystem::path{resolved_asset_name}, std::ios::binary};
    if (!stream.is_open()) {
        return -1;
    }
    stream.read(reinterpret_cast<char*>(out_buf), static_cast<std::streamsize>(req_nbytes));
    *nbytes = static_cast<std::uint64_t>(stream.gcount());
    return 0;
}

// The stage metadata a load consumes, plus the time codes a reader needs to
// interpret the rest. Anything not listed here is the root layer's alone.
[[nodiscard]] auto has_all_stage_metas(const lightusd::StageMetas& metas) -> bool
{
    return
        metas.upAxis.authored()             &&
        metas.metersPerUnit.authored()      &&
        metas.timeCodesPerSecond.authored() &&
        metas.framesPerSecond.authored()    &&
        metas.startTimeCode.authored()      &&
        metas.endTimeCode.authored()        &&
        metas.kilogramsPerUnit.authored()   &&
        !metas.defaultPrim.str().empty()    &&
        metas.customLayerDataAuthored;
}

// Fill the stage metadata `dst` does not author from the weaker layer `src`.
// USD resolves a layer stack's metadata local-first: the root layer's opinion
// wins over every sublayer, and among the sublayers the earlier entry of the
// `subLayers` array is the stronger one.
void fill_absent_stage_metas(lightusd::StageMetas& dst, const lightusd::StageMetas& src)
{
    if (!dst.upAxis.authored() && src.upAxis.authored()) {
        dst.upAxis.set_value(src.upAxis.get_value());
    }
    if (!dst.metersPerUnit.authored() && src.metersPerUnit.authored()) {
        dst.metersPerUnit.set_value(src.metersPerUnit.get_value());
    }
    if (!dst.timeCodesPerSecond.authored() && src.timeCodesPerSecond.authored()) {
        dst.timeCodesPerSecond.set_value(src.timeCodesPerSecond.get_value());
    }
    if (!dst.framesPerSecond.authored() && src.framesPerSecond.authored()) {
        dst.framesPerSecond.set_value(src.framesPerSecond.get_value());
    }
    if (!dst.startTimeCode.authored() && src.startTimeCode.authored()) {
        dst.startTimeCode.set_value(src.startTimeCode.get_value());
    }
    if (!dst.endTimeCode.authored() && src.endTimeCode.authored()) {
        dst.endTimeCode.set_value(src.endTimeCode.get_value());
    }
    if (!dst.kilogramsPerUnit.authored() && src.kilogramsPerUnit.authored()) {
        dst.kilogramsPerUnit.set_value(src.kilogramsPerUnit.get_value());
    }
    if (dst.defaultPrim.str().empty() && !src.defaultPrim.str().empty()) {
        dst.defaultPrim = src.defaultPrim;
    }
    if (!dst.customLayerDataAuthored && src.customLayerDataAuthored) {
        dst.customLayerData         = src.customLayerData;
        dst.customLayerDataAuthored = true;
    }
}

// Walk `layer`'s sublayer tree in strength order, filling in the stage
// metadata the stronger layers leave unauthored. LightUSD's
// CompositeSublayers() keeps the root layer's metadata and drops every
// sublayer's, so this second walk is what carries a weaker layer's `upAxis` /
// `metersPerUnit` / ... to the composed stage. It parses each sublayer file
// again; the layers that author stage metadata are the small ones (a sublayer
// stack's heavy content sits below a reference or a payload), and the walk
// stops as soon as every field is authored.
void fill_stage_metas_from_sublayers(
    lightusd::AssetResolutionResolver& resolver,
    const lightusd::Layer&             layer,
    lightusd::StageMetas&              metas,
    std::set<std::string>&             visited,
    const std::size_t                  depth
)
{
    if (depth >= c_max_sublayer_depth) {
        return;
    }
    for (const lightusd::SubLayer& sub_layer : layer.metas().subLayers) {
        if (has_all_stage_metas(metas)) {
            return;
        }
        const std::string asset_path = sub_layer.assetPath.GetAssetPath();
        if (asset_path.empty()) {
            continue;
        }
        resolver.set_current_working_path(layer.get_current_working_path());
        resolver.set_search_paths(layer.get_asset_search_paths());
        const std::string resolved_path = resolver.resolve(asset_path);
        if (resolved_path.empty() || (visited.count(resolved_path) != 0)) {
            continue;
        }
        visited.insert(resolved_path);

        lightusd::Layer          sub_layer_layer;
        lightusd::USDLoadOptions options{};
        std::string              warning;
        std::string              error;
        if (!lightusd::LoadLayerFromFile(resolved_path, &sub_layer_layer, &warning, &error, options)) {
            continue;
        }
        const std::filesystem::path sub_layer_path{resolved_path};
        const std::string           sub_layer_dir =
            sub_layer_path.has_parent_path() ? sub_layer_path.parent_path().generic_string() : std::string{"."};
        sub_layer_layer.set_asset_resolution_state(sub_layer_dir, std::vector<std::string>{sub_layer_dir});

        fill_absent_stage_metas(metas, sub_layer_layer.metas());
        fill_stage_metas_from_sublayers(resolver, sub_layer_layer, metas, visited, depth + 1);
    }
}

// Whether any prim spec below `spec` carries a variant block with a `def`
// child: what hoist_variant_prims has work to do for.
[[nodiscard]] auto has_variant_prims(const lightusd::PrimSpec& spec) -> bool
{
    for (const std::pair<const std::string, lightusd::VariantSetSpec>& set : spec.variantSets()) {
        for (const std::pair<const std::string, lightusd::PrimSpec>& variant : set.second.variantSet) {
            for (const lightusd::PrimSpec& child : variant.second.children()) {
                if (child.specifier() == lightusd::Specifier::Def) {
                    return true;
                }
            }
        }
    }
    for (const lightusd::PrimSpec& child : spec.children()) {
        if (has_variant_prims(child)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto has_variant_prims(const lightusd::Layer& layer) -> bool
{
    for (const std::pair<const std::string, lightusd::PrimSpec>& entry : layer.primspecs()) {
        if (has_variant_prims(entry.second)) {
            return true;
        }
    }
    return false;
}

// The name a hoisted prim gets below `spec`, by the M2 sibling-unique rule
// over the child prim specs `spec` already holds.
[[nodiscard]] auto make_unique_child_name(const lightusd::PrimSpec& spec, const std::string& wanted_name) -> std::string
{
    return erhe::Hierarchy::make_unique_name(
        wanted_name,
        [&spec](const std::string_view candidate) -> bool {
            for (const lightusd::PrimSpec& child : spec.children()) {
                if (child.name() == candidate) {
                    return true;
                }
            }
            return false;
        }
    );
}

// Copy the `def` children of every variant block of `spec` into `spec` itself
// (doc/usd-compatibility-plan.md X4). Every variant's prims end up in the
// tree, whichever variant is selected: the selected variant's are left as
// they are, and the rest are marked `active = false`, which prunes them from
// the render, the pick and the simulation the way USD's own `active` does
// (X2). A switch is then a property write like every other one, not a rebuild
// of the tree.
//
// The block itself is left where it is, so nothing about the variant set is
// lost: the importer reads the blocks off the same layer for the property
// opinions and the material bindings, and the writer puts each prim back
// inside its own variant block under the name recorded here.
void hoist_variant_prims(
    const std::string&                path,
    lightusd::PrimSpec&               spec,
    std::vector<Variant_prim_record>& records
)
{
    const lightusd::VariantSelectionMap& selection = spec.get_variant_selection_map();
    for (const std::pair<const std::string, lightusd::VariantSetSpec>& set : spec.variantSets()) {
        const lightusd::VariantSelectionMap::const_iterator i = selection.find(set.first);
        // The same selection rule the importer applies: the layer's own
        // `variants` opinion, or the first variant when it authors none.
        const std::string selected = (i != selection.end())
            ? i->second
            : (set.second.variantSet.empty() ? std::string{} : set.second.variantSet.begin()->first);
        for (const std::pair<const std::string, lightusd::PrimSpec>& variant : set.second.variantSet) {
            for (const lightusd::PrimSpec& child : variant.second.children()) {
                if (child.specifier() != lightusd::Specifier::Def) {
                    continue; // an `over` child is an opinion on a prim the tree already has
                }
                lightusd::PrimSpec hoisted     = child;
                const std::string  unique_name = make_unique_child_name(spec, child.name());
                hoisted.name() = unique_name;
                if (variant.first != selected) {
                    hoisted.metas().set_active(false);
                }
                records.push_back(
                    Variant_prim_record{
                        .carrier_path  = path,
                        .set_name      = set.first,
                        .variant_name  = variant.first,
                        .prim_name     = unique_name,
                        .authored_name = child.name()
                    }
                );
                spec.children().push_back(std::move(hoisted));
            }
        }
    }
    // By index: the loop above appended to this same vector, and a hoisted
    // prim can carry variant sets of its own.
    for (std::size_t index = 0; index < spec.children().size(); ++index) {
        hoist_variant_prims(path + "/" + spec.children()[index].name(), spec.children()[index], records);
    }
}

void hoist_variant_prims(lightusd::Layer& layer, std::vector<Variant_prim_record>& records)
{
    ERHE_PROFILE_FUNCTION();

    for (std::pair<const std::string, lightusd::PrimSpec>& entry : layer.primspecs()) {
        hoist_variant_prims("/" + entry.first, entry.second, records);
    }
}

// Replace `stage` with the composition of `root_layer` and its `subLayers`
// (the L of USD's LIVRPS), and hand the composed layer back in
// `composed_layer_out`: it is the spec tree behind the composed prims, and
// every read that asks which layer authored a thing goes to it. Returns false
// when the composition could not be built, leaving `stage` and
// `composed_layer_out` as they were - a file whose sublayers cannot be
// resolved still opens with the root layer's own content, as it does in USD.
[[nodiscard]] auto compose_sublayers(
    const std::filesystem::path&      path,
    const lightusd::Layer&            root_layer,
    lightusd::Stage&                  stage,
    lightusd::Layer&                  composed_layer_out,
    std::vector<Variant_prim_record>& variant_prims,
    std::string&                      warning
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const std::string filename = path.generic_string();
    const std::string base_dir = path.has_parent_path() ? path.parent_path().generic_string() : std::string{"."};

    lightusd::AssetResolutionResolver resolver;
    resolver.set_current_working_path(base_dir);
    resolver.set_search_paths(std::vector<std::string>{base_dir});
    resolver.set_allow_parent_relative_paths(true);
    lightusd::AssetResolutionHandler asset_handler{};
    asset_handler.resolve_fun = &resolve_asset_handler;
    asset_handler.size_fun    = &asset_size_handler;
    asset_handler.read_fun    = &read_asset_handler;
    resolver.register_wildcard_asset_resolution_handler(asset_handler);

    std::string load_warning;
    std::string load_error;

    lightusd::Layer                       composed_layer;
    lightusd::SublayersCompositionOptions sublayer_options{};
    sublayer_options.max_depth = static_cast<std::uint32_t>(c_max_sublayer_depth);
    // Parent-relative asset paths are resolved, not refused (see the asset
    // resolution handler above).
    sublayer_options.allow_parent_relative_paths = true;
    if (!lightusd::CompositeSublayers(resolver, root_layer, &composed_layer, &load_warning, &load_error, sublayer_options)) {
        log_usd->warn("USD '{}': subLayer composition failed: {}", filename, load_error);
        warning += load_error;
        return false;
    }

    // The composed layer keeps the root layer's metadata (CompositeSublayers
    // copies it), and the fields the root leaves unauthored come from the
    // sublayers, strongest first. The `subLayers` list itself stays on the
    // composed stage as the record of which layers went into it: a save writes
    // one layer holding the composed content and names them in its log line.
    lightusd::StageMetas  metas = root_layer.metas();
    std::set<std::string> visited;
    fill_stage_metas_from_sublayers(resolver, root_layer, metas, visited, 0);
    composed_layer.metas() = metas;

    hoist_variant_prims(composed_layer, variant_prims);

    // LayerToStage consumes the layer it builds from, so the layer the
    // importer reads is a copy taken here. It is the same tree the prims come
    // from: one copy of the composed spec tree per load.
    lightusd::Layer kept_layer = composed_layer;

    lightusd::Stage composed_stage;
    if (!lightusd::LayerToStage(std::move(composed_layer), &composed_stage, &load_warning, &load_error)) {
        log_usd->warn("USD '{}': the composed subLayer stack could not be built into a stage: {}", filename, load_error);
        warning += load_error;
        return false;
    }
    if (!load_warning.empty()) {
        warning += load_warning;
        log_usd->warn("USD '{}': subLayer composition: {}", filename, load_warning);
    }

    // A composed layer stack has no single authored top-level order - each
    // layer authors its own - and LightUSD's Layer holds its prim specs in a
    // hash map, so the order LayerToStage produces is the map's. Sorting the
    // top-level prims by name makes the composed tree the same on every run
    // and every platform.
    std::vector<lightusd::Prim>& root_prims = composed_stage.root_prims();
    std::sort(
        root_prims.begin(),
        root_prims.end(),
        [](const lightusd::Prim& lhs, const lightusd::Prim& rhs) {
            return lhs.element_name() < rhs.element_name();
        }
    );

    stage              = std::move(composed_stage);
    composed_layer_out = std::move(kept_layer);
    return true;
}

// Replace `stage` with `layer` built into a stage with the prims of every
// variant block hoisted into the tree, and make `layer` that hoisted layer:
// what a file without subLayers needs, the composition above doing it for one
// that has them. A stage that cannot be built leaves `stage` as the loader
// built it and `layer` as it was read - with the variants' prims absent, as
// they were before this - and is reported.
void compose_variant_prims(
    const std::filesystem::path&      path,
    lightusd::Layer&                  layer,
    lightusd::Stage&                  stage,
    std::vector<Variant_prim_record>& variant_prims,
    std::string&                      warning
)
{
    ERHE_PROFILE_FUNCTION();

    const std::string filename = path.generic_string();
    lightusd::Layer   hoisted_layer = layer;
    hoist_variant_prims(hoisted_layer, variant_prims);

    // LayerToStage consumes the layer it builds from, and the hoisted layer is
    // what the importer reads afterwards, so the stage is built from a copy.
    lightusd::Layer build_layer = hoisted_layer;

    std::string     load_warning;
    std::string     load_error;
    lightusd::Stage composed_stage;
    if (!lightusd::LayerToStage(std::move(build_layer), &composed_stage, &load_warning, &load_error)) {
        log_usd->warn("USD '{}': the prims of the variant blocks could not be built into a stage: {}", filename, load_error);
        warning += load_error;
        variant_prims.clear();
        return;
    }
    if (!load_warning.empty()) {
        warning += load_warning;
        log_usd->warn("USD '{}': variant prims: {}", filename, load_warning);
    }
    stage = std::move(composed_stage);
    layer = std::move(hoisted_layer);
}

// The absolute paths of the marked `NodeGraph` prims one layer authors
// (doc/usd-texture-graphs-plan.md 2.1): an erhe texture graph, as opposed to a
// foreign shading network (R5).
void collect_node_graph_paths(
    const std::string&         path,
    const lightusd::PrimSpec&  spec,
    std::set<std::string>&     out_paths
)
{
    if (
        (spec.typeName() == c_node_graph_prim_type_name) &&
        (spec.props().find(std::string{c_node_graph_format_attribute}) != spec.props().end())
    ) {
        out_paths.insert(path);
        return;
    }
    for (const lightusd::PrimSpec& child : spec.children()) {
        collect_node_graph_paths(path + "/" + child.name(), child, out_paths);
    }
}

void collect_node_graph_paths(const lightusd::Layer& layer, std::set<std::string>& out_paths)
{
    for (const std::pair<const std::string, lightusd::PrimSpec>& entry : layer.primspecs()) {
        collect_node_graph_paths("/" + entry.first, entry.second, out_paths);
    }
}

// Whether a connection target lies inside one of the graphs.
[[nodiscard]] auto targets_node_graph(
    const lightusd::Attribute&   attribute,
    const std::set<std::string>& graph_paths
) -> bool
{
    for (const lightusd::Path& connection : attribute.connections()) {
        const lightusd::tstring_view prim_part = connection.prim_part();
        const std::string            target{prim_part.data(), prim_part.size()};
        for (const std::string& graph_path : graph_paths) {
            if ((target == graph_path) || (target.compare(0, graph_path.size() + 1, graph_path + "/") == 0)) {
                return true;
            }
        }
    }
    return false;
}

// Take every connection into one of the graphs out of `spec` and its subtree.
// A `UsdPreviewSurface` input wired to a `NodeGraph` output is what R2 spells,
// and Tydra's render-scene conversion fails the whole material over it: the
// connection resolves to no `UsdUVTexture`, which is a hard error there. erhe
// resolves the graph itself, off the layer this strips a copy of, so the stage
// Tydra sees carries the graph prims without the wiring and the material takes
// its schema fallback for that input - which is what the caller then rebinds
// to the rebuilt graph asset.
void strip_node_graph_connections(lightusd::PrimSpec& spec, const std::set<std::string>& graph_paths)
{
    std::vector<std::string> removed;
    for (const std::pair<const std::string, lightusd::Property>& property : spec.props()) {
        if (property.second.is_attribute() && targets_node_graph(property.second.get_attribute(), graph_paths)) {
            removed.push_back(property.first);
        }
    }
    for (const std::string& name : removed) {
        spec.props().erase(name);
    }
    for (lightusd::PrimSpec& child : spec.children()) {
        strip_node_graph_connections(child, graph_paths);
    }
}

// Replace `stage` with `layer` stripped of its texture-graph wiring and built
// into a stage, the way compose_variant_prims replaces it with the hoisted
// layer. `layer` itself keeps the wiring: it is what the importer reads the
// graphs and the material slot bindings off. A stage that cannot be built
// leaves `stage` as it was and is reported.
void compose_node_graph_stage(
    const std::filesystem::path& path,
    const lightusd::Layer&       layer,
    lightusd::Stage&             stage,
    const std::set<std::string>& graph_paths,
    std::string&                 warning
)
{
    ERHE_PROFILE_FUNCTION();

    const std::string filename = path.generic_string();
    lightusd::Layer   stripped = layer;
    for (std::pair<const std::string, lightusd::PrimSpec>& entry : stripped.primspecs()) {
        strip_node_graph_connections(entry.second, graph_paths);
    }

    std::string     load_warning;
    std::string     load_error;
    lightusd::Stage composed_stage;
    if (!lightusd::LayerToStage(std::move(stripped), &composed_stage, &load_warning, &load_error)) {
        log_usd->warn("USD '{}': the texture graph wiring could not be resolved: {}", filename, load_error);
        warning += load_error;
        return;
    }
    if (!load_warning.empty()) {
        warning += load_warning;
        log_usd->warn("USD '{}': texture graphs: {}", filename, load_warning);
    }
    stage = std::move(composed_stage);
}

} // anonymous namespace

auto load_stage(const std::filesystem::path& path) -> Load_stage_result
{
    ERHE_PROFILE_FUNCTION();

    Load_stage_result result{};

    std::unique_ptr<Stage::Impl> impl = std::make_unique<Stage::Impl>();
    impl->source_path = path;

    const std::string        filename = path.generic_string();
    lightusd::USDLoadOptions options{};
    std::string              warning;
    std::string              error;
    const bool               loaded = lightusd::LoadUSDFromFile(filename, &impl->stage, &warning, &error, options);

    result.warning = warning;
    if (!warning.empty()) {
        log_usd->warn("Loading USD stage '{}': {}", filename, warning);
    }
    if (!loaded) {
        result.error = error.empty() ? std::string{"USD load failed"} : error;
        log_usd->error("Loading USD stage '{}' failed: {}", filename, result.error);
        return result;
    }

    // LightUSD composes nothing at load, so the stage the reader built holds
    // the root layer alone, unresolved: its `subLayers` are not merged in and
    // the prims its variant blocks author are absent. Both are erhe's own
    // composition, and both happen here, before anything converts the stage,
    // so everything downstream - the Tydra conversion, the prim tree, a
    // referenced file loaded through this same function - sees one composed
    // stage.
    //
    // The layer is read once and kept, and what is kept is the composed layer:
    // the importer takes the `class` prims, the `over` opinions and the
    // `variantSet` blocks off it rather than parsing the file a third time,
    // and a prim any layer of the stack authors carries them the way a
    // root-layer prim does.
    const std::string base_dir = path.has_parent_path() ? path.parent_path().generic_string() : std::string{"."};
    std::string       layer_warning;
    std::string       layer_error;
    impl->layer_ok = lightusd::LoadLayerFromFile(filename, &impl->layer, &layer_warning, &layer_error, options);
    if (!impl->layer_ok) {
        log_usd->info("USD '{}': the root layer could not be read for composition: {}", filename, layer_error);
    } else {
        impl->layer.set_asset_resolution_state(base_dir, std::vector<std::string>{base_dir});
        const std::size_t sublayer_count = impl->stage.metas().subLayers.size();
        // A `.usdz` archive resolves its asset paths through the archive
        // rather than through the file system, and the composition resolver
        // reaches the file system only, so an archive keeps the stage the
        // loader built: a sublayer inside one is named rather than composed,
        // and the prims of a variant block inside one stay uncarried.
        if (path.extension() == ".usdz") {
            if (sublayer_count > 0) {
                log_usd->warn(
                    "USD '{}': {} subLayer(s) inside a .usdz archive are not composed - the stage holds the root layer alone",
                    filename, sublayer_count
                );
            }
            if (has_variant_prims(impl->layer)) {
                log_usd->warn(
                    "USD '{}': the prims a variant block inside a .usdz archive adds are not carried",
                    filename
                );
            }
        } else if (sublayer_count > 0) {
            lightusd::Layer composed_layer;
            if (compose_sublayers(path, impl->layer, impl->stage, composed_layer, impl->variant_prims, result.warning)) {
                impl->layer = std::move(composed_layer);
                log_usd->info("USD '{}': composed {} subLayer(s)", filename, sublayer_count);
            }
        } else if (has_variant_prims(impl->layer)) {
            compose_variant_prims(path, impl->layer, impl->stage, impl->variant_prims, result.warning);
        }
        if (!impl->variant_prims.empty()) {
            log_usd->info("USD '{}': {} prim(s) of variant blocks are in the tree", filename, impl->variant_prims.size());
        }
        // An erhe texture graph is wiring Tydra cannot follow, so the stage it
        // converts is built without it (doc/usd-texture-graphs-plan.md 2.3).
        std::set<std::string> node_graph_paths;
        collect_node_graph_paths(impl->layer, node_graph_paths);
        if (!node_graph_paths.empty()) {
            if (path.extension() == ".usdz") {
                log_usd->warn(
                    "USD '{}': {} texture graph(s) inside a .usdz archive are not resolved",
                    filename, node_graph_paths.size()
                );
            } else {
                compose_node_graph_stage(path, impl->layer, impl->stage, node_graph_paths, result.warning);
                log_usd->info("USD '{}': resolved {} texture graph(s)", filename, node_graph_paths.size());
            }
        }
    }

    log_usd->info("Loaded USD stage '{}'", filename);
    result.stage = std::make_unique<Stage>(std::move(impl));
    return result;
}

namespace {

void count_prims(
    const lightusd::Prim&               prim,
    std::size_t&                        prim_count,
    std::map<std::string, std::size_t>& type_counts,
    std::vector<Layer_reference>&       layers
)
{
    ++prim_count;
    ++type_counts[prim.type_name()];

    const lightusd::PrimMetas& metas = prim.metas();
    if (metas.references.has_value()) {
        for (const std::pair<lightusd::ListEditQual, std::vector<lightusd::Reference>>& list_op : *metas.references) {
            for (const lightusd::Reference& reference : list_op.second) {
                const std::string& asset_path = reference.asset_path.GetAssetPath();
                if (!asset_path.empty()) {
                    layers.push_back(Layer_reference{"reference", asset_path});
                }
            }
        }
    }
    if (metas.payload.has_value()) {
        for (const std::pair<lightusd::ListEditQual, std::vector<lightusd::Payload>>& list_op : *metas.payload) {
            for (const lightusd::Payload& payload : list_op.second) {
                const std::string& asset_path = payload.asset_path.GetAssetPath();
                if (!asset_path.empty()) {
                    layers.push_back(Layer_reference{"payload", asset_path});
                }
            }
        }
    }

    for (const lightusd::Prim& child : prim.children()) {
        count_prims(child, prim_count, type_counts, layers);
    }
}

auto to_string(const lightusd::Axis axis) -> std::string
{
    switch (axis) {
        case lightusd::Axis::X: return "X";
        case lightusd::Axis::Y: return "Y";
        case lightusd::Axis::Z: return "Z";
        default:                return "invalid";
    }
}

} // anonymous namespace

auto describe_stage(const Stage& stage) -> Stage_description
{
    ERHE_PROFILE_FUNCTION();

    const Stage::Impl&    impl        = stage.get_impl();
    const lightusd::Stage& usd_stage  = impl.stage;
    const lightusd::StageMetas& metas = usd_stage.metas();

    Stage_description description{};
    description.up_axis         = to_string(metas.upAxis.get_value());
    description.default_prim    = metas.defaultPrim.str();
    description.meters_per_unit = metas.metersPerUnit.get_value();

    description.layers.push_back(Layer_reference{"root", impl.source_path.generic_string()});
    for (const lightusd::SubLayer& sub_layer : metas.subLayers) {
        description.layers.push_back(Layer_reference{"sublayer", sub_layer.assetPath.GetAssetPath()});
    }

    std::map<std::string, std::size_t> type_counts;
    for (const lightusd::Prim& prim : usd_stage.root_prims()) {
        count_prims(prim, description.prim_count, type_counts, description.layers);
    }

    description.prim_types.reserve(type_counts.size());
    for (const std::pair<const std::string, std::size_t>& entry : type_counts) {
        description.prim_types.push_back(Prim_type_count{entry.first, entry.second});
    }

    return description;
}

} // namespace erhe::usd
