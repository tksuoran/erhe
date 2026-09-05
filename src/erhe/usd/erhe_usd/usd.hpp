#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

} // namespace erhe::usd
