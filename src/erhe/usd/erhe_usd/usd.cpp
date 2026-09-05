#include "erhe_usd/usd.hpp"
#include "erhe_usd/usd_impl.hpp"
#include "erhe_usd/usd_log.hpp"

#include "erhe_profile/profile.hpp"

// LightUSD headers. Together with usd_import.cpp this is the only place in
// erhe that includes them; everything the rest of erhe sees is declared in
// usd.hpp.
#include "lightusd.hh"
#include "core/prim.hh"
#include "core/prim-metas.hh"
#include "stage.hh"

#include <algorithm>
#include <map>
#include <utility>

namespace erhe::usd {

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
