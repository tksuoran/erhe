#include "content_library/content_library.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace editor {

template <typename T>
auto get(const std::vector<std::shared_ptr<erhe::Item_base>>& items, const std::size_t index = 0) -> std::shared_ptr<T>
{
    std::size_t i = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        if (item->get_type() == erhe::Item_type::content_library_node) {
            const auto node = std::dynamic_pointer_cast<Content_library_node>(item);
            if (node) {
                const auto node_item = node->item;
                if (node_item) {
                    if (!erhe::utility::test_all_rhs_bits_set(node_item->get_type(), T::get_static_type())) {
                        continue;
                    }
                    if (i == index) {
                        return std::static_pointer_cast<T>(node_item);
                    }
                    ++i;
                }
            }
        } else {
            const auto node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                const std::vector<std::shared_ptr<erhe::scene::Node_attachment>>& attachments = node->get_attachments();
                for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment_item : attachments) {
                    if (!erhe::utility::test_all_rhs_bits_set(attachment_item->get_type(), T::get_static_type())) {
                        continue;
                    }
                    if (i == index) {
                        return std::dynamic_pointer_cast<T>(attachment_item);
                    }
                }
            }

            if (!erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
                continue;
            }
            if (i == index) {
                return std::static_pointer_cast<T>(item);
            }
            ++i;
        }
    }
    return {};
}

template <typename T>
auto get_all(const std::vector<std::shared_ptr<erhe::Item_base>>& items) -> std::vector<std::shared_ptr<T>>
{
    std::vector<std::shared_ptr<T>> result;

    std::size_t i = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        if (item->get_type() == erhe::Item_type::content_library_node) {
            const auto node = std::dynamic_pointer_cast<Content_library_node>(item);
            if (node) {
                const auto node_item = node->item;
                if (node_item) {
                    if (!erhe::utility::test_all_rhs_bits_set(node_item->get_type(), T::get_static_type())) {
                        continue;
                    }
                    result.push_back(std::static_pointer_cast<T>(node_item));
                    ++i;
                }
            }
        } else {
            const auto node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
            if (node) {
                const std::vector<std::shared_ptr<erhe::scene::Node_attachment>>& attachments = node->get_attachments();
                for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment_item : attachments) {
                    if (!erhe::utility::test_all_rhs_bits_set(attachment_item->get_type(), T::get_static_type())) {
                        continue;
                    }
                    result.push_back(std::dynamic_pointer_cast<T>(attachment_item));
                }
            }

            if (!erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
                continue;
            }
            result.push_back(std::static_pointer_cast<T>(item));
            ++i;
        }
    }
    return result;
}

template <typename T>
auto count(const std::vector<std::shared_ptr<erhe::Item_base>>& items) -> std::size_t
{
    std::size_t i = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : items) {
        if (!item) {
            continue;
        }
        if (!erhe::utility::test_all_rhs_bits_set(item->get_type(), T::get_static_type())) {
            continue;
        }
        ++i;
    }
    return i;
}

}
