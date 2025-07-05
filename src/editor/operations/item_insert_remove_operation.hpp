#pragma once

#include "operations/ioperation.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

namespace editor {

class Item_parent_change_operation;

class Item_insert_remove_operation : public Operation
{
public:
    enum class Mode : unsigned int {
        insert = 0,
        remove
    };

    [[nodiscard]] static auto inverse(const Mode mode) -> Mode
    {
        switch (mode) {
            //using enum Mode;
            case Mode::insert: return Mode::remove;
            case Mode::remove: return Mode::insert;
            default: {
                ERHE_FATAL("Bad Item_insert_remove_operation::Mode %04x", static_cast<unsigned int>(mode));
                // unreachable return Mode::insert;
            }
        }
    }

    class Parameters
    {
    public:
        App_context&                     context;
        std::shared_ptr<erhe::Hierarchy> item;
        std::shared_ptr<erhe::Hierarchy> parent;
        Mode                             mode;
        std::size_t                      index_in_parent = 0;
    };

    explicit Item_insert_remove_operation(const Parameters& parameters);

    // Implements Operation
    auto describe() const -> std::string override;
    void execute (App_context& context)  override;
    void undo    (App_context& context)  override;

private:
    Mode                                                       m_mode;
    std::shared_ptr<erhe::Hierarchy>                           m_item         {};
    std::shared_ptr<erhe::Hierarchy>                           m_before_parent{};
    std::shared_ptr<erhe::Hierarchy>                           m_after_parent {};
    std::vector<std::shared_ptr<Item_parent_change_operation>> m_parent_changes;
    std::size_t                                                m_index_in_parent{};
    std::size_t                                                m_index_in_parent_insert{};

    erhe::Item_host*                              m_scene_host{nullptr};
    std::vector<std::shared_ptr<erhe::Item_base>> m_selection_before;
    std::vector<std::shared_ptr<erhe::Item_base>> m_selection_after;
};

}
