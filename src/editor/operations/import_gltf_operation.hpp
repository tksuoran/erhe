#pragma once

#include "operations/operation.hpp"
#include "parsers/gltf.hpp"

#include <memory>

namespace editor {

class Compound_operation;

// A glTF import that can give back its memory.
//
// The problem it solves: a recorded import owns every object it created - the
// content-library entries, the whole detached node subtree, and (through
// Async_raytrace_kickoff_operation) the target Scene_root - so undoing a large
// import frees nothing until the entry itself is destroyed. Loading a scene,
// undoing it, and loading another one therefore holds both in memory.
//
// The fix: the payload (the built compound) is droppable, and everything
// needed to build it again lives in the recipe. Redo re-reads the file.
//
// Dropping is only safe when nothing recorded after this import survives in
// the redo stack, because those entries hold raw shared_ptrs to this import's
// content and a rebuild produces fresh objects with fresh ids. That condition
// is decided by Operation_stack, which calls on_lossless_undo() only on the
// top-level entry it undid and only when the redo stack holds nothing else.
//
// See doc/reloadable-asset-loads.md.
class Import_gltf_operation : public Operation
{
public:
    Import_gltf_operation(
        Gltf_import_recipe                   recipe,
        std::shared_ptr<Prepared_gltf_parse> prepared_parse = {}
    );
    ~Import_gltf_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;
    void on_lossless_undo(App_context& context) override;
    [[nodiscard]] auto has_droppable_payload() const -> bool override;
    void drop_payload() override;

    // False once the payload has been dropped: the operation then holds only
    // the recipe, and the next execute() rebuilds by re-reading the file.
    [[nodiscard]] auto has_payload() const -> bool;

    // Drops the built compound. Everything it owned - library entries, the
    // detached subtree, the retained Scene_root - is released with it, and the
    // Asset_reference userships its children held unregister through their
    // destructors. Public because free_undone_loads releases payloads the
    // automatic gate declined.
    void release_payload();

    [[nodiscard]] auto get_path() const -> const std::filesystem::path&;

private:
    // Builds the compound from the recipe: consumes the prepared parse on the
    // first execute, re-reads the file on any later one.
    [[nodiscard]] auto build(App_context& context) -> bool;

    void update_description();

    Gltf_import_recipe                   m_recipe;
    std::shared_ptr<Prepared_gltf_parse> m_prepared_parse; // consumed by the first build
    std::shared_ptr<Operation>           m_compound;       // null == payload dropped
    // Rebuild count, for the description and for tests distinguishing a redo
    // that replayed a kept payload from one that re-read the file.
    std::size_t                          m_rebuild_count{0};
};

}
