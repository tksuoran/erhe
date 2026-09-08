#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

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

// Replace `stage` with the composition of the root layer at `path` and its
// `subLayers` (the L of USD's LIVRPS). Returns false when the root layer or
// the composition could not be built, leaving `stage` as it was - a file whose
// sublayers cannot be resolved still opens with the root layer's own content,
// as it does in USD.
[[nodiscard]] auto compose_sublayers(
    const std::filesystem::path& path,
    lightusd::Stage&             stage,
    std::string&                 warning
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

    lightusd::USDLoadOptions load_options{};
    std::string              load_warning;
    std::string              load_error;
    lightusd::Layer          root_layer;
    if (!lightusd::LoadLayerFromFile(filename, &root_layer, &load_warning, &load_error, load_options)) {
        log_usd->warn("USD '{}': the root layer could not be re-read for subLayer composition: {}", filename, load_error);
        warning += load_error;
        return false;
    }
    root_layer.set_asset_resolution_state(base_dir, std::vector<std::string>{base_dir});

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

    stage = std::move(composed_stage);
    return true;
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

    // A root layer's `subLayers` are the weakest opinions of its layer stack
    // and LightUSD composes nothing at load, so the stage the reader built
    // holds the root layer alone. Compose the stack here, before anything
    // converts the stage: everything downstream - the Tydra conversion, the
    // prim tree, a referenced file loaded through this same function - sees
    // one composed stage. The re-read costs one extra parse of the root layer
    // and is paid only by a file that has sublayers.
    if (!impl->stage.metas().subLayers.empty()) {
        const std::size_t sublayer_count = impl->stage.metas().subLayers.size();
        // A `.usdz` archive resolves its asset paths through the archive
        // rather than through the file system, and the composition resolver
        // reaches the file system only, so a sublayer inside an archive is
        // named rather than composed.
        if (path.extension() == ".usdz") {
            log_usd->warn(
                "USD '{}': {} subLayer(s) inside a .usdz archive are not composed - the stage holds the root layer alone",
                filename, sublayer_count
            );
        } else if (compose_sublayers(path, impl->stage, result.warning)) {
            log_usd->info("USD '{}': composed {} subLayer(s)", filename, sublayer_count);
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
