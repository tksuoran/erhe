#pragma once

#include "erhe_item/unique_id.hpp"

#include <string>
#include <unordered_set>

namespace erhe {
    class Item_base;
}

namespace editor {

class App_context;

class Operation
{
public:
    virtual ~Operation() noexcept;

    virtual void execute(App_context& context) = 0;
    virtual void undo   (App_context& context) = 0;

    // R5.4 (asset-manager plan resolution 7, mechanism b): report the items
    // this operation RETAINS that cannot themselves hold an Asset_reference
    // - e.g. a node subtree kept for undo whose mesh primitives pin
    // materials. Asset_manager::request_unload consults the whole
    // Operation_stack through this and refuses container unload with the
    // collective "undo/redo history" label while any recorded operation
    // retains one of the container's assets. Default: retains nothing.
    // Direct asset holders (material change, library attach, animation
    // edits) instead declare usership via an adopted Asset_reference member
    // (mechanism a) and need no override here.
    virtual void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const;

    [[nodiscard]] auto        describe  () const -> const std::string&;
    [[nodiscard]] inline auto get_serial() const -> std::size_t { return m_id.get_id(); }
    [[nodiscard]] auto        get_error () const -> const std::string&;
    [[nodiscard]] auto        has_error () const -> bool;

    void set_description(std::string&& description);
    void set_error      (std::string error);

private:
    erhe::Unique_id<Operation> m_id{};
    std::string                m_description;
    std::string                m_error;
};

}
