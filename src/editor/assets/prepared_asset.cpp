#include "assets/prepared_asset.hpp"

#include "assets/item_keys.hpp"
#include "editor_log.hpp"

#include "erhe_asset_store/asset_store.hpp"
#include "erhe_asset_store/item_kind.hpp"
#include "erhe_file/file.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace editor {

auto is_prepared_asset_path(const std::filesystem::path& path) -> bool
{
    return path.extension() == std::filesystem::path{".erhe_prepared"};
}

Prepared_asset::Prepared_asset(
    const std::filesystem::path&                    path,
    std::unique_ptr<erhe::asset_store::Asset_store> store,
    std::string                                     target
)
    : m_path  {path}
    , m_store {std::move(store)}
    , m_target{std::move(target)}
{
}

Prepared_asset::~Prepared_asset() noexcept
{
    log_parsers->info("Prepared asset '{}' closed", erhe::file::to_string(m_path));
}

auto Prepared_asset::get_path() const -> const std::filesystem::path&
{
    return m_path;
}

auto Prepared_asset::get_target() const -> const std::string&
{
    return m_target;
}

auto Prepared_asset::get_store() const -> const erhe::asset_store::Asset_store&
{
    return *m_store;
}

Prepared_item_reader::Prepared_item_reader(std::shared_ptr<const Prepared_asset> prepared_asset, const erhe::asset_store::Key& key)
    : m_prepared_asset{std::move(prepared_asset)}
    , m_key           {key}
{
}

auto Prepared_item_reader::read_at(const std::uint64_t offset, const std::span<std::byte> out) const -> bool
{
    return m_prepared_asset->get_store().read_payload(m_key, offset, out);
}

void Prepared_item_reader::close() const
{
    m_prepared_asset->get_store().close_payload_handles();
}

auto make_prepared_buffer_resolver(const std::shared_ptr<const Prepared_asset>& prepared_asset) -> erhe::gltf::Gltf_buffer_resolver
{
    return [prepared_asset](const std::string_view uri) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader> {
        const std::optional<erhe::asset_store::Key> key = parse_item_uri(uri);
        if (!key.has_value()) {
            return {};
        }
        if (!prepared_asset->get_store().contains(key.value())) {
            // Copy writes every named item with the Scene item: the asset was changed after its Export.
            log_parsers->error("Prepared asset '{}' lacks item {} its Scene item names", erhe::file::to_string(prepared_asset->get_path()), key->to_hex());
            return {};
        }
        return std::make_shared<Prepared_item_reader>(prepared_asset, key.value());
    };
}

namespace {

constexpr std::string_view c_temporary_infix = ".tmp-";

}

auto make_prepared_temporary_path(const std::filesystem::path& prepared_asset_path) -> std::filesystem::path
{
    std::random_device random_device;
    const std::uint64_t random = (static_cast<std::uint64_t>(random_device()) << 32u) ^ static_cast<std::uint64_t>(random_device());
    std::filesystem::path path = prepared_asset_path;
    path += fmt::format("{}{:016x}", c_temporary_infix, random);
    return path;
}

void remove_prepared_temporary(const std::filesystem::path& temporary_path)
{
    std::error_code error_code;
    std::filesystem::remove(temporary_path, error_code);
    std::filesystem::path journal = temporary_path;
    journal += "-journal";
    std::filesystem::remove(journal, error_code);
}

void remove_stale_prepared_temporaries(const std::filesystem::path& prepared_asset_path)
{
    const std::string prefix = erhe::file::to_string(prepared_asset_path.filename()) + std::string{c_temporary_infix};
    std::filesystem::path directory = prepared_asset_path.parent_path();
    if (directory.empty()) {
        directory = ".";
    }
    std::error_code error_code;
    std::vector<std::filesystem::path> stale;
    for (std::filesystem::directory_iterator i{directory, error_code}, end; !error_code && (i != end); i.increment(error_code)) {
        if (erhe::file::to_string(i->path().filename()).starts_with(prefix)) {
            stale.push_back(i->path());
        }
    }
    for (const std::filesystem::path& path : stale) {
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        log_parsers->warn(
            "Prepared asset '{}': removed '{}', left by an Export that did not finish{}",
            erhe::file::to_string(prepared_asset_path), erhe::file::to_string(path), remove_error ? (" - removal failed: " + remove_error.message()) : std::string{}
        );
    }
}

namespace {

[[nodiscard]] auto is_readable_file(const std::filesystem::path& path) -> bool
{
    std::error_code error_code;
    if (!std::filesystem::is_regular_file(path, error_code)) {
        return false;
    }
    std::ifstream file{path, std::ios::binary};
    return file.is_open();
}

// True when the store records a source file that is readable; out_recorded_source gets any recorded source.
[[nodiscard]] auto find_readable_source(
    const erhe::asset_store::Asset_store& store,
    const std::filesystem::path&          prepared_asset_path,
    std::filesystem::path&                out_recorded_source
) -> bool
{
    const std::optional<erhe::asset_store::Source_record> source_record = store.get_source_record();
    if (!source_record.has_value() || source_record->relative_path.empty()) {
        return false;
    }
    out_recorded_source = (prepared_asset_path.parent_path() / std::filesystem::path{source_record->relative_path}).lexically_normal();
    return is_readable_file(out_recorded_source);
}

[[nodiscard]] auto make_failed(std::string error) -> Prepared_open
{
    Prepared_open result{};
    result.kind  = Prepared_open_kind::failed;
    result.error = std::move(error);
    return result;
}

}

namespace {

constexpr std::string_view c_stamp_separator = " | ";

[[nodiscard]] auto join_stamps(const std::span<const std::string> stamps) -> std::string
{
    std::string text;
    for (const std::string& stamp : stamps) {
        if (!text.empty()) {
            text += c_stamp_separator;
        }
        text += stamp;
    }
    return text;
}

[[nodiscard]] auto split_stamps(const std::string_view text) -> std::vector<std::string>
{
    std::vector<std::string> stamps;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find(c_stamp_separator, begin);
        const std::string_view stamp = text.substr(begin, (end == std::string_view::npos) ? std::string_view::npos : (end - begin));
        if (!stamp.empty()) {
            stamps.emplace_back(stamp);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + c_stamp_separator.size();
    }
    return stamps;
}

[[nodiscard]] auto open_prepared(const std::filesystem::path& prepared_asset_path, const Prepared_build_formats& build) -> Prepared_open
{
    using erhe::asset_store::Asset_store;
    const std::string path_string = erhe::file::to_string(prepared_asset_path);
    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    std::string open_error;
    std::unique_ptr<Asset_store> store = Asset_store::open(prepared_asset_path, erhe::asset_store::Open_mode::prepared_read_only, open_error);
    if (!store) {
        return make_failed(fmt::format("prepared asset '{}' cannot be opened: {}", path_string, open_error));
    }

    Prepared_open result{};
    if (find_readable_source(*store, prepared_asset_path, result.source_path)) {
        result.kind = Prepared_open_kind::open_source;
        return result;
    }

    const std::optional<erhe::asset_store::Prepared_scene_record> scene_record = store->get_prepared_scene();
    if (!scene_record.has_value()) {
        return make_failed(fmt::format("prepared asset '{}' records no Scene item", path_string));
    }
    // R11.4, R11.6: Copy records the stamps of every item it puts in, with the Scene key.
    std::string missing_record;
    const std::optional<Prepared_stamp_records> records = read_prepared_stamp_records(*store, missing_record);
    if (!records.has_value()) {
        return make_failed(fmt::format(
            "prepared asset '{}' cannot be opened: it has no {} record; this build checks a prepared asset's compatibility by the format stamps an Export records (export it again)",
            path_string, missing_record
        ));
    }
    const std::optional<erhe::asset_store::Item_info> scene_info = store->get_info(scene_record->scene_key);
    if (!scene_info.has_value() || (scene_info->kind != erhe::asset_store::Item_kind::scene)) {
        return make_failed(fmt::format("prepared asset '{}' lacks its Scene item {}", path_string, scene_record->scene_key.to_hex()));
    }
    result.format_check = check_prepared_formats(path_string, scene_record->target, records.value(), build);
    log_parsers->info(
        "Prepared asset '{}': stamp records checked in {:.3f} ms", path_string,
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count()
    );
    if (!result.format_check.error.empty()) {
        Prepared_open failed = make_failed(result.format_check.error);
        failed.format_check = std::move(result.format_check);
        return failed;
    }
    result.kind            = Prepared_open_kind::prepared_run;
    result.scene_key       = scene_record->scene_key;
    result.scene_item_size = scene_info->payload_size;
    result.prepared_asset  = std::make_shared<Prepared_asset>(prepared_asset_path, std::move(store), scene_record->target);
    return result;
}

} // anonymous namespace

auto write_prepared_stamp_records(erhe::asset_store::Write_transaction& transaction, const Prepared_stamp_records& records) -> bool
{
    std::vector<std::string> texture_stamps = records.texture_stamps;
    std::sort(texture_stamps.begin(), texture_stamps.end());
    texture_stamps.erase(std::unique(texture_stamps.begin(), texture_stamps.end()), texture_stamps.end());
    return
        transaction.set_record(c_prepared_mesh_stamp_record,     records.mesh_stamp) &&
        transaction.set_record(c_prepared_bvh_stamp_record,      records.bvh_stamp) &&
        transaction.set_record(c_prepared_scene_stamp_record,    records.scene_stamp) &&
        transaction.set_record(c_prepared_texture_stamps_record, join_stamps(texture_stamps));
}

auto read_prepared_stamp_records(const erhe::asset_store::Asset_store& store, std::string& out_missing_record) -> std::optional<Prepared_stamp_records>
{
    const auto read = [&store, &out_missing_record](const std::string_view name, std::string& out) -> bool {
        std::optional<std::string> value = store.get_record(name);
        if (!value.has_value()) {
            if (out_missing_record.empty()) {
                out_missing_record = std::string{name};
            }
            return false;
        }
        out = std::move(value.value());
        return true;
    };
    Prepared_stamp_records records{};
    std::string texture_stamps;
    const bool mesh_ok    = read(c_prepared_mesh_stamp_record,     records.mesh_stamp);
    const bool bvh_ok     = read(c_prepared_bvh_stamp_record,      records.bvh_stamp);
    const bool scene_ok   = read(c_prepared_scene_stamp_record,    records.scene_stamp);
    const bool texture_ok = read(c_prepared_texture_stamps_record, texture_stamps);
    if (!mesh_ok || !bvh_ok || !scene_ok || !texture_ok) {
        return std::nullopt;
    }
    records.texture_stamps = split_stamps(texture_stamps);
    return records;
}

auto write_prepared_asset(const Prepared_write_request& request) -> Prepared_write_result
{
    using erhe::asset_store::Asset_store;
    Prepared_write_result result{};
    remove_stale_prepared_temporaries(request.prepared_asset_path);
    const std::filesystem::path temporary_path = make_prepared_temporary_path(request.prepared_asset_path);
    const std::string           temporary_text = erhe::file::to_string(temporary_path);
    {
        std::string open_error;
        std::unique_ptr<Asset_store> prepared = Asset_store::open(temporary_path, erhe::asset_store::Open_mode::prepared_create, open_error);
        if (!prepared) {
            result.error = fmt::format("'{}' cannot be created: {}", temporary_text, open_error);
            remove_prepared_temporary(temporary_path);
            return result;
        }
        {
            erhe::asset_store::Write_transaction transaction{*prepared};
            if (!transaction.is_active()) {
                result.error = fmt::format("'{}' takes no writes", temporary_text);
            }
            for (const Prepared_write_item& item : request.items) {
                if (!result.error.empty()) {
                    break;
                }
                if ((item.store == nullptr) || !item.store->contains(item.key)) {
                    result.error = fmt::format(
                        "item {} ({}) the Scene item names is not in the editor asset '{}'",
                        item.key.to_hex(), c_str(item.kind), (item.store != nullptr) ? erhe::file::to_string(item.store->get_path()) : std::string{"(none)"}
                    );
                    break;
                }
                if (!transaction.copy_item_from(*item.store, item.key)) {
                    result.error = fmt::format("item {} ({}) was not written into '{}'", item.key.to_hex(), c_str(item.kind), temporary_text);
                    break;
                }
                ++result.written[item.kind];
                if (request.advance) {
                    request.advance();
                }
            }
            if (result.error.empty()) {
                const bool records_written =
                    transaction.set_source_record(request.source) &&
                    transaction.set_prepared_scene(request.scene) &&
                    write_prepared_stamp_records(transaction, request.stamps);
                if (!records_written || !transaction.commit()) {
                    result.error = fmt::format("the write of '{}' was rolled back", temporary_text);
                }
            }
        }
        if (result.error.empty()) {
            result.item_count = prepared->get_item_count().value_or(0);
            result.bytes      = prepared->get_database_size().value_or(0);
        }
    }
    if (!result.error.empty()) {
        result.written.clear();
        remove_prepared_temporary(temporary_path);
        return result;
    }
    // Replaces the target at once (doc/editor/prepared_assets.md "Export" on a target held open).
    std::error_code rename_error;
    std::filesystem::rename(temporary_path, request.prepared_asset_path, rename_error);
    if (rename_error) {
        result.error = fmt::format(
            "'{}' cannot replace '{}': {}", temporary_text, erhe::file::to_string(request.prepared_asset_path), rename_error.message()
        );
        result.written.clear();
        remove_prepared_temporary(temporary_path);
    }
    return result;
}

auto check_prepared_formats(
    const std::string_view        prepared_asset_name,
    const std::string_view        asset_target,
    const Prepared_stamp_records& records,
    const Prepared_build_formats& build
) -> Prepared_format_check
{
    using erhe::asset_store::Item_kind;
    const auto reads = [](const std::vector<std::string>& stamps, const std::string& stamp) -> bool {
        return std::find(stamps.begin(), stamps.end(), stamp) != stamps.end();
    };

    std::vector<Prepared_stamp_mismatch> mismatches;
    if (records.mesh_stamp != build.mesh_stamp) {
        mismatches.push_back(Prepared_stamp_mismatch{
            .kind = c_str(Item_kind::local_mesh), .stored_stamp = records.mesh_stamp, .expected_stamp = build.mesh_stamp, .effect = Prepared_stamp_effect::open_fails
        });
    }
    const bool without_bvh = (records.bvh_stamp == c_prepared_no_bvh_stamp);
    if (!without_bvh && (records.bvh_stamp != build.bvh_stamp)) {
        mismatches.push_back(Prepared_stamp_mismatch{
            .kind = c_str(Item_kind::raytrace_bvh), .stored_stamp = records.bvh_stamp, .expected_stamp = build.bvh_stamp, .effect = Prepared_stamp_effect::rebuilt_in_memory
        });
    }
    if (!reads(build.scene_stamps, records.scene_stamp)) {
        mismatches.push_back(Prepared_stamp_mismatch{
            .kind = c_str(Item_kind::scene), .stored_stamp = records.scene_stamp, .expected_stamp = join_stamps(build.scene_stamps), .effect = Prepared_stamp_effect::open_fails
        });
    }
    for (const std::string& stamp : records.texture_stamps) {
        if (!reads(build.texture_stamps, stamp)) {
            mismatches.push_back(Prepared_stamp_mismatch{
                .kind = c_str(Item_kind::texture), .stored_stamp = stamp, .expected_stamp = join_stamps(build.texture_stamps), .effect = Prepared_stamp_effect::open_fails
            });
        }
    }

    Prepared_format_check check{};
    std::string failures;
    for (Prepared_stamp_mismatch& mismatch : mismatches) {
        const char* effect_text = "the BVHs are built in memory from their meshes";
        std::string kinds = mismatch.kind;
        if (mismatch.kind == c_str(Item_kind::local_mesh)) {
            kinds = fmt::format("{} and {}", c_str(Item_kind::local_mesh), c_str(Item_kind::world_mesh));
        }
        if (mismatch.effect == Prepared_stamp_effect::open_fails) {
            effect_text =
                (mismatch.kind == c_str(Item_kind::scene))   ? "this build does not read its images" :
                (mismatch.kind == c_str(Item_kind::texture)) ? "this build does not sample or decode their block format" :
                "a prepared asset holds nothing to rebuild them from";
        }
        std::string line = fmt::format(
            "the {} items of '{}' (target {}) carry format stamp '{}' (recorded at Export) where this build has '{}' - {}",
            kinds, prepared_asset_name, asset_target, mismatch.stored_stamp, mismatch.expected_stamp, effect_text
        );
        if (mismatch.effect == Prepared_stamp_effect::open_fails) {
            failures += failures.empty() ? line : ("; " + line);
        }
        check.notices.push_back(std::move(line));
        check.mismatches.push_back(std::move(mismatch));
    }
    // R11.5: an asset exported with raytrace_bvh off names no BVH items.
    if (without_bvh) {
        check.exported_without_bvh = true;
        check.notices.push_back(fmt::format(
            "the {} and {} items of '{}' (target {}) have no {} items (exported with raytrace_bvh off) - their BVHs are built in memory",
            c_str(Item_kind::local_mesh), c_str(Item_kind::world_mesh), prepared_asset_name, asset_target, c_str(Item_kind::raytrace_bvh)
        ));
    }
    if (asset_target != build.target) {
        check.notices.push_back(fmt::format(
            "'{}' is a prepared asset of target {}; this build runs target {}", prepared_asset_name, asset_target, build.target
        ));
    }
    if (!failures.empty()) {
        check.error = fmt::format("prepared asset '{}' of target {} is not compatible with this build: {}", prepared_asset_name, asset_target, failures);
    }
    return check;
}

auto get_stored_bvh_use(const Prepared_format_check& check) -> erhe::gltf::Stored_bvh_use
{
    for (const Prepared_stamp_mismatch& mismatch : check.mismatches) {
        if (mismatch.kind == c_str(erhe::asset_store::Item_kind::raytrace_bvh)) {
            return erhe::gltf::Stored_bvh_use::ignore;
        }
    }
    return erhe::gltf::Stored_bvh_use::use;
}

auto resolve_prepared_open(const std::filesystem::path& prepared_asset_path, const Prepared_build_formats& build) -> Prepared_open
{
    return open_prepared(prepared_asset_path, build);
}

auto make_scene_item_reader(const Prepared_open& prepared_open) -> std::shared_ptr<const erhe::gltf::Gltf_buffer_reader>
{
    if ((prepared_open.kind != Prepared_open_kind::prepared_run) || !prepared_open.prepared_asset) {
        return {};
    }
    return std::make_shared<Prepared_item_reader>(prepared_open.prepared_asset, prepared_open.scene_key);
}

auto get_prepared_open_scene_name(const std::filesystem::path& prepared_asset_path, const Prepared_open& prepared_open) -> std::string
{
    if (prepared_open.kind == Prepared_open_kind::open_source) {
        return erhe::file::to_string(prepared_open.source_path.stem());
    }
    return erhe::file::to_string(prepared_asset_path.stem());
}

void log_prepared_open(const std::filesystem::path& prepared_asset_path, const Prepared_open& prepared_open)
{
    if (prepared_open.kind == Prepared_open_kind::open_source) {
        log_parsers->info(
            "Open prepared asset '{}': its source '{}' is readable - opening the source instead",
            erhe::file::to_string(prepared_asset_path), erhe::file::to_string(prepared_open.source_path)
        );
    } else if (prepared_open.kind == Prepared_open_kind::prepared_run) {
        log_parsers->info(
            "Prepared run of '{}' (target {}): its source {} cannot be read",
            erhe::file::to_string(prepared_asset_path),
            prepared_open.prepared_asset->get_target(),
            prepared_open.source_path.empty()
                ? std::string{"is not recorded and"}
                : fmt::format("'{}'", erhe::file::to_string(prepared_open.source_path))
        );
        for (const std::string& notice : prepared_open.format_check.notices) {
            log_parsers->warn("Prepared run: {}", notice);
        }
    }
}

}
