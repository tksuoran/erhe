#include "operations/import_gltf_operation.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_file/file.hpp"
#include "erhe_primitive/build_info.hpp"

#include <fmt/format.h>

#include <chrono>
#include <filesystem>

namespace editor {

Import_gltf_operation::Import_gltf_operation(
    Gltf_import_recipe                   recipe,
    std::shared_ptr<Prepared_gltf_parse> prepared_parse
)
    : m_recipe        {std::move(recipe)}
    , m_prepared_parse{std::move(prepared_parse)}
{
    update_description();
}

Import_gltf_operation::~Import_gltf_operation() noexcept = default;

void Import_gltf_operation::update_description()
{
    set_description(
        fmt::format(
            "[{}] Import glTF {}{}",
            get_serial(),
            erhe::file::to_string(m_recipe.path.filename()),
            has_payload() ? "" : " (unloaded)"
        )
    );
}

auto Import_gltf_operation::has_payload() const -> bool
{
    return static_cast<bool>(m_compound);
}

auto Import_gltf_operation::get_path() const -> const std::filesystem::path&
{
    return m_recipe.path;
}

auto Import_gltf_operation::build(App_context& context) -> bool
{
    const std::shared_ptr<Scene_root> scene_root = m_recipe.scene_root.lock();
    if (!scene_root) {
        // The target scene is gone. Nothing to rebuild into; leave the payload
        // null so undo() is a no-op and the entry stays inert.
        set_error(
            fmt::format(
                "cannot import '{}': the target scene no longer exists",
                erhe::file::to_string(m_recipe.path.filename())
            )
        );
        log_parsers->warn("{}", get_error());
        return false;
    }

    const bool rebuilding = (m_prepared_parse == nullptr) && (m_rebuild_count > 0);
    if (rebuilding && !std::filesystem::exists(m_recipe.path)) {
        set_error(
            fmt::format(
                "cannot re-import '{}': the file no longer exists",
                erhe::file::to_string(m_recipe.path)
            )
        );
        log_parsers->warn("{}", get_error());
        return false;
    }

    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    // The rebuild must go through the interactive transfer queue: it publishes
    // immediately, whereas the loader queue gates publication on the loader
    // watermark (see make_import_build_info).
    m_compound = make_import_gltf_operation(
        context,
        make_import_build_info(context),
        scene_root,
        m_recipe.path,
        m_recipe.materials_as_references,
        m_recipe.fit_view_to_content,
        m_prepared_parse.get(),
        &m_recipe
    );
    // Consumed: gltf_data is moved out, so a later rebuild must re-read the
    // file rather than re-use a moved-from parse.
    m_prepared_parse.reset();

    if (!m_compound) {
        set_error(fmt::format("import of '{}' produced no operation", erhe::file::to_string(m_recipe.path)));
        return false;
    }

    if (m_rebuild_count > 0) {
        const double duration_ms =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start_time
                ).count()
            ) * 1.0e-6;
        log_parsers->info(
            "re-read '{}' for redo in {:.1f} ms",
            erhe::file::to_string(m_recipe.path.filename()),
            duration_ms
        );
    }
    ++m_rebuild_count;
    return true;
}

void Import_gltf_operation::execute(App_context& context)
{
    if (!m_compound) {
        if (!build(context)) {
            update_description();
            return;
        }
    }
    m_compound->execute(context);
    update_description();
}

void Import_gltf_operation::undo(App_context& context)
{
    if (!m_compound) {
        // A failed rebuild left nothing to undo. Inert, not an error.
        return;
    }
    m_compound->undo(context);
}

void Import_gltf_operation::on_lossless_undo(App_context& context)
{
    static_cast<void>(context);
    if (!m_compound) {
        return;
    }
    log_parsers->info(
        "dropping the loaded content of '{}'; a redo re-reads the file",
        erhe::file::to_string(m_recipe.path.filename())
    );
    release_payload();
}

auto Import_gltf_operation::has_droppable_payload() const -> bool
{
    return has_payload();
}

void Import_gltf_operation::drop_payload()
{
    release_payload();
}

void Import_gltf_operation::release_payload()
{
    m_compound.reset();
    update_description();
}

void Import_gltf_operation::collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const
{
    // With the payload dropped this operation retains nothing, which is what
    // lets Asset_manager::unload_record stop refusing on the undo history.
    if (m_compound) {
        m_compound->collect_item_references(out_items);
    }
}

}
