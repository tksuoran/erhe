#pragma once

#include "operations/ioperation.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_verify/verify.hpp"

namespace editor
{

class Item_parent_change_operation;
class Selection;

class Scene_item_operation
    : public IOperation
{
public:
    enum class Mode : unsigned int {
        insert = 0,
        remove
    };

    static auto inverse(const Mode mode) -> Mode
    {
        switch (mode) {
            //using enum Mode;
            case Mode::insert: return Mode::remove;
            case Mode::remove: return Mode::insert;
            default: {
                ERHE_FATAL("Bad Context::Mode %04x", static_cast<unsigned int>(mode));
                // unreachable return Mode::insert;
            }
        }
    }
};

class Item_insert_remove_operation
    : public Scene_item_operation
{
public:
    class Parameters
    {
    public:
        Editor_context&                  context;
        std::shared_ptr<erhe::Hierarchy> item;
        std::shared_ptr<erhe::Hierarchy> parent;
        Mode                             mode;
    };

    explicit Item_insert_remove_operation(const Parameters& parameters);

    // Implements IOperation
    [[nodiscard]] auto describe() const -> std::string override;
    void execute(Editor_context& context) override;
    void undo   (Editor_context& context) override;

private:
    Mode                                                       m_mode;
    std::shared_ptr<erhe::Hierarchy>                           m_item         {};
    std::shared_ptr<erhe::Hierarchy>                           m_before_parent{};
    std::shared_ptr<erhe::Hierarchy>                           m_after_parent {};
    std::vector<std::shared_ptr<Item_parent_change_operation>> m_parent_changes;

    erhe::Item_host*                         m_scene_host{nullptr};
    std::vector<std::shared_ptr<erhe::Item>> m_selection_before;
    std::vector<std::shared_ptr<erhe::Item>> m_selection_after;
};

}
