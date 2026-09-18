#pragma once

#include "operations/operation.hpp"

#include "erhe_property/dependency_object.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace erhe           { class Item_base; }
namespace erhe::property { class Property_set; }

namespace editor {

// Undoable swap of one item's style (doc/erhe/property_system.md D25):
// `after` replaces the item's style on execute, `before` on undo; nullptr
// is "no style". Local values are untouched by either direction.
class Style_set_operation : public Operation
{
public:
    Style_set_operation(
        const std::shared_ptr<erhe::Item_base>&               item,
        std::shared_ptr<const erhe::property::Dependency_object> before,
        std::shared_ptr<const erhe::property::Dependency_object> after
    );
    ~Style_set_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;
    void collect_item_references(std::unordered_set<const erhe::Item_base*>& out_items) const override;

private:
    void apply(App_context& context, const std::shared_ptr<const erhe::property::Dependency_object>& style);

    std::shared_ptr<erhe::Item_base>                         m_item;
    std::shared_ptr<const erhe::property::Dependency_object> m_before;
    std::shared_ptr<const erhe::property::Dependency_object> m_after;
};

class App_context;
class Compound_operation;

// A style item from a bag of values (doc/editor/style_library.md R3): a Style
// named `name` (made unique in the folder) targeting the first item's
// class, inserted into the Styles folder of the first item's scene library
// and assigned to every unsealed item of that class - one undo entry.
// Null when no item takes it or the bag holds nothing the class has.
[[nodiscard]] auto make_style_from_values(
    App_context&                                         context,
    const std::vector<std::shared_ptr<erhe::Item_base>>& items,
    const erhe::property::Property_set&                  values,
    std::string_view                                     name
) -> std::shared_ptr<Compound_operation>;

} // namespace editor
