#pragma once

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_asset_store/item_kind.hpp"
#include "erhe_asset_store/key.hpp"
#include "erhe_gltf/gltf.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Opening a prepared asset (doc/editor/prepared_assets.md "Prepared run").
namespace editor {

// ".erhe_prepared"
[[nodiscard]] auto is_prepared_asset_path(const std::filesystem::path& path) -> bool;

// A prepared asset opened read-only for a prepared run; the store closes with
// the last owner (the Scene_root and the buffer readers of its primitives).
class Prepared_asset
{
public:
    Prepared_asset(
        const std::filesystem::path&                    path,
        std::unique_ptr<erhe::asset_store::Asset_store> store,
        std::string                                     target
    );
    ~Prepared_asset() noexcept;
    Prepared_asset(const Prepared_asset&) = delete;
    auto operator=(const Prepared_asset&) -> Prepared_asset& = delete;

    [[nodiscard]] auto get_path  () const -> const std::filesystem::path&;
    [[nodiscard]] auto get_target() const -> const std::string&;
    [[nodiscard]] auto get_store () const -> const erhe::asset_store::Asset_store&;

private:
    std::filesystem::path                           m_path;
    std::unique_ptr<erhe::asset_store::Asset_store> m_store;
    std::string                                     m_target;
};

// The payload of one item as a glTF buffer.
class Prepared_item_reader : public erhe::gltf::Gltf_buffer_reader
{
public:
    Prepared_item_reader(std::shared_ptr<const Prepared_asset> prepared_asset, const erhe::asset_store::Key& key);

    [[nodiscard]] auto read_at(std::uint64_t offset, std::span<std::byte> out) const -> bool override;
    void close() const override;

private:
    std::shared_ptr<const Prepared_asset> m_prepared_asset;
    erhe::asset_store::Key                m_key;
};

// Maps erhe-item:<key> uris to readers over the items of the prepared asset;
// null for any other uri and for a key the asset does not hold.
[[nodiscard]] auto make_prepared_buffer_resolver(const std::shared_ptr<const Prepared_asset>& prepared_asset) -> erhe::gltf::Gltf_buffer_resolver;

// Export's temporary file of a prepared asset (R9.3): "<file name>.tmp-<16 hex digits>" beside it.
[[nodiscard]] auto make_prepared_temporary_path(const std::filesystem::path& prepared_asset_path) -> std::filesystem::path;
// Removes the temporary file and its rollback journal.
void remove_prepared_temporary(const std::filesystem::path& temporary_path);
// Removes (logged) every temporary file of the prepared asset an earlier, killed Export left.
void remove_stale_prepared_temporaries(const std::filesystem::path& prepared_asset_path);

// What the running build reads (doc/editor/prepared_assets.md "Format stamps").
class Prepared_build_formats
{
public:
    std::string              target;       // the target of this platform: "quest" on Android, else "pc"
    std::string              mesh_stamp;   // the Local-space mesh stamp of the running Mesh_memory
    std::string              bvh_stamp;    // the Raytrace BVH stamp of the running BVH library
    std::vector<std::string> scene_stamps; // the Scene item stamps whose images this build samples or decodes
    std::vector<std::string> texture_stamps; // the Texture item stamps whose block format this build samples or decodes
};

// Prepared asset records of the exporting build's stamps, written by Copy with
// prepared.scene_key (doc/editor/prepared_assets.md "Format stamps").
inline constexpr std::string_view c_prepared_mesh_stamp_record     = "prepared.mesh_stamp";
inline constexpr std::string_view c_prepared_bvh_stamp_record      = "prepared.bvh_stamp";
inline constexpr std::string_view c_prepared_scene_stamp_record    = "prepared.scene_stamp";
inline constexpr std::string_view c_prepared_texture_stamps_record = "prepared.texture_stamps";
// prepared.bvh_stamp of an asset without Raytrace BVH items (raytrace_bvh off, R11.5).
inline constexpr std::string_view c_prepared_no_bvh_stamp          = "none";

// The stamps of every item a prepared asset's Scene item names; one Export makes them all.
class Prepared_stamp_records
{
public:
    std::string              mesh_stamp;     // Local-space and World-space mesh items
    std::string              bvh_stamp;      // Raytrace BVH items, or c_prepared_no_bvh_stamp
    std::string              scene_stamp;    // the Scene item
    std::vector<std::string> texture_stamps; // the distinct stamps of its Texture items, sorted
};

// Writes the records into the transaction that writes prepared.scene_key.
[[nodiscard]] auto write_prepared_stamp_records(erhe::asset_store::Write_transaction& transaction, const Prepared_stamp_records& records) -> bool;
// Nullopt with out_missing_record named when a record is absent (an asset made before the records).
[[nodiscard]] auto read_prepared_stamp_records(
    const erhe::asset_store::Asset_store& store,
    std::string&                          out_missing_record
) -> std::optional<Prepared_stamp_records>;

// One item Copy writes: its key, the kind Ensure asked it as and the editor asset that holds it.
class Prepared_write_item
{
public:
    erhe::asset_store::Key                key{};
    erhe::asset_store::Item_kind          kind{erhe::asset_store::Item_kind::editor_data};
    const erhe::asset_store::Asset_store* store{nullptr};
};

// Copy's whole write of a prepared asset (R9.3, doc/editor/prepared_assets.md "Export").
class Prepared_write_request
{
public:
    std::filesystem::path                      prepared_asset_path;
    std::vector<Prepared_write_item>           items;
    erhe::asset_store::Source_record           source;
    erhe::asset_store::Prepared_scene_record   scene;
    Prepared_stamp_records                     stamps;
    std::function<void()>                      advance; // after each item; may be empty
};

class Prepared_write_result
{
public:
    std::string                                  error; // empty when the target was replaced
    std::map<erhe::asset_store::Item_kind, std::size_t> written;
    std::uint64_t                                item_count{0};
    std::uint64_t                                bytes{0};
};

// Writes the items and records into a new temporary file and renames it over
// the target; on any failure the temporary file is removed and the target untouched.
[[nodiscard]] auto write_prepared_asset(const Prepared_write_request& request) -> Prepared_write_result;

enum class Prepared_stamp_effect : unsigned int
{
    rebuilt_in_memory = 0, // Raytrace BVHs: built from their meshes in place of the stored ones (R11.4)
    open_fails             // mesh, Texture or Scene items: nothing in the asset to rebuild them from
};

// One recorded stamp that differs from this build's (R11.4, R11.6).
class Prepared_stamp_mismatch
{
public:
    std::string           kind;
    std::string           stored_stamp;
    std::string           expected_stamp;
    Prepared_stamp_effect effect{Prepared_stamp_effect::open_fails};
};

class Prepared_format_check
{
public:
    std::vector<Prepared_stamp_mismatch> mismatches;
    std::vector<std::string>             notices; // one line per mismatch, raytrace_bvh off, and the other target
    std::string                          error;   // the open fails when not empty
    bool                                 exported_without_bvh{false}; // prepared.bvh_stamp is c_prepared_no_bvh_stamp (R11.5)
};

// The recorded stamps against the build: mesh, Scene or Texture stamps it does not
// read fail the open; another BVH stamp or none builds the BVHs in memory.
[[nodiscard]] auto check_prepared_formats(
    std::string_view              prepared_asset_name,
    std::string_view              asset_target,
    const Prepared_stamp_records& records,
    const Prepared_build_formats& build
) -> Prepared_format_check;

// ignore when the recorded BVH stamp is another: the load builds the BVHs in memory (R11.4).
[[nodiscard]] auto get_stored_bvh_use(const Prepared_format_check& check) -> erhe::gltf::Stored_bvh_use;

enum class Prepared_open_kind : unsigned int
{
    failed = 0,
    open_source,  // the source asset is readable: open it instead (R11.1)
    prepared_run
};

// What opening a .erhe_prepared path resolves to; runs on any thread.
class Prepared_open
{
public:
    Prepared_open_kind              kind{Prepared_open_kind::failed};
    std::string                     error;       // failed
    std::filesystem::path           source_path; // the recorded source (empty when none is recorded)
    std::shared_ptr<Prepared_asset> prepared_asset;
    erhe::asset_store::Key          scene_key{};       // prepared_run: the Scene item (GLB)
    std::uint64_t                   scene_item_size{0};
    Prepared_format_check           format_check;
};

// The open with its format check (R11.1, R11.4, R11.6); reads the records, not the items.
// A caller that reports the outcome before queueing the open (MCP load_scene) hands it to the load.
[[nodiscard]] auto resolve_prepared_open(const std::filesystem::path& prepared_asset_path, const Prepared_build_formats& build) -> Prepared_open;
// The parse's reader of the Scene item of a prepared_run (Gltf_parse_arguments::glb_reader); null otherwise.
[[nodiscard]] auto make_scene_item_reader(const Prepared_open& prepared_open) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader>;
// The name of the scene the open makes: the stem of its readable source, else the path's own stem.
[[nodiscard]] auto get_prepared_open_scene_name(const std::filesystem::path& prepared_asset_path, const Prepared_open& prepared_open) -> std::string;
// The notice of an open_source or prepared_run outcome (R11.1), with the format notices of a prepared run.
void log_prepared_open(const std::filesystem::path& prepared_asset_path, const Prepared_open& prepared_open);

}
