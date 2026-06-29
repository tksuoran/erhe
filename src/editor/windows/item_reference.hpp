#pragma once

#include "erhe_item/item.hpp"

#include <memory>
#include <span>

namespace editor {

class App_context;

// Options controlling an Item_reference field. All fields have sensible defaults.
class Item_reference_options
{
public:
    const char*                                       none_text{"(none)"};       // shown when the reference is empty
    std::span<const std::shared_ptr<erhe::Item_base>> candidates{};              // non-empty enables a picker popup
    bool                                              accept_content_library_node{false}; // also accept a Content_library_node payload, unwrapping to its item
    bool                                              show_select_button{true};  // button that adds the referenced item to the selection
    bool                                              show_clear_button {true};  // button that clears the reference
};

// Generic, reusable reference field for a single erhe::Item.
//
// The field is both a drag-and-drop target (filtered by allowed_types, drawn with the
// standard drop-target highlight) and a drag-and-drop source for the referenced item. It
// optionally offers a picker popup (when options.candidates is non-empty) and buttons to add
// the referenced item to the selection / clear the reference.
//
// io_value is in/out: the current value on entry, the new value on return. label is used only
// as the ImGui id scope; no visible label is drawn (callers that want one draw it themselves).
// Returns true when io_value changed this frame (set via drop or picker, or cleared).
[[nodiscard]] auto item_reference_imgui(
    App_context&                      context,
    const char*                       label,
    std::shared_ptr<erhe::Item_base>& io_value,
    uint64_t                          allowed_types,
    const Item_reference_options&     options = {}
) -> bool;

// Type-safe convenience wrapper bound to a weak_ptr<T> member. allowed_types defaults to T's
// own type bit. On change the value is cast back to T (null when cleared or incompatible).
template <typename T>
[[nodiscard]] auto item_reference_imgui(
    App_context&                  context,
    const char*                   label,
    std::weak_ptr<T>&             reference,
    uint64_t                      allowed_types = T::get_static_type(),
    const Item_reference_options& options       = {}
) -> bool
{
    std::shared_ptr<erhe::Item_base> value   = reference.lock();
    const bool                       changed = item_reference_imgui(context, label, value, allowed_types, options);
    if (changed) {
        reference = std::dynamic_pointer_cast<T>(value);
    }
    return changed;
}

} // namespace editor
