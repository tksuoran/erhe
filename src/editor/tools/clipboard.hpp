#pragma once

#include "erhe_property/property_set.hpp"
#include "assets/asset_reference.hpp"
#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <memory>
#include <unordered_set>
#include <vector>

namespace erhe {
    class Item_base;
    class Hierarchy;
}
namespace erhe::commands {
    class Commands;
}

namespace editor {

class Content_library;
class App_context;
class App_message_bus;
class Scene_view;
class Scene_root;
class Tools;

class Clipboard_paste_command : public erhe::commands::Command
{
public:
    Clipboard_paste_command(erhe::commands::Commands& commands, App_context& context);

    void try_ready()         override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

class Clipboard : public erhe::commands::Command_host
{
public:
    Clipboard(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus
    );

    // Commands
    auto try_ready() -> bool;
    auto try_paste(const std::shared_ptr<erhe::Hierarchy>& target_parent, std::size_t index_in_parent) -> bool;
    auto try_paste() -> bool;

    // Public API
    void set_contents(const std::vector<std::shared_ptr<erhe::Item_base>>& items);
    void set_contents(const std::shared_ptr<erhe::Item_base>& item);
    auto get_contents() -> const std::vector<std::shared_ptr<erhe::Item_base>>&;

    // Copy / Paste Properties (doc/erhe/property_system.md D12): a bag of
    // (property, value) read from one item's local values, applied to the
    // selection by Property_set_apply_operation. Independent of the item
    // contents above.
    void set_property_contents(const erhe::property::Property_set& properties, std::string_view source_name);
    [[nodiscard]] auto get_property_contents     () const -> const erhe::property::Property_set&;
    [[nodiscard]] auto get_property_contents_name() const -> const std::string&; // the item they were copied from (a style's name, D25)
    [[nodiscard]] auto has_property_contents() const -> bool;

    // Scene-close leak watchdog support: the items the clipboard contents
    // keep alive on purpose - the held items themselves plus, transitively,
    // their child prims, mesh materials and material textures - so that
    // paste-after-source-scene-close works. The watchdog reports these as
    // intentional pins, not leaks (same contract as the inventory / hotbar
    // slot pins).
    void collect_pinned_items(std::unordered_set<const erhe::Item_base*>& out_pinned) const;

private:
    [[nodiscard]] auto resolve_paste_target() -> std::shared_ptr<erhe::Hierarchy>;

    // R5.6: the clipboard is a DECLARED user of every manager-owned asset
    // its contents reach (mesh primitive materials, directly copied
    // assets), rebuilt on every set_contents. This makes copy -> close ->
    // paste-elsewhere a named courtesy-unload refusal ("clipboard") instead
    // of an undeclared-user warning; the defining record survives the
    // close, so the pasted content keeps rendering the one shared object.
    void update_asset_userships();

    erhe::message_bus::Subscription<Hover_scene_view_message>      m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_scene_item_tree_message> m_hover_scene_item_tree_subscription;
    Clipboard_paste_command                       m_paste_command;
    App_context&                                  m_context;
    std::vector<std::shared_ptr<erhe::Item_base>> m_contents;
    erhe::property::Property_set                  m_property_contents;
    std::string                                   m_property_contents_name;
    std::vector<Asset_reference>                  m_asset_userships;

    Scene_view*                                   m_hover_scene_view          {nullptr};
    Scene_view*                                   m_last_hover_scene_view     {nullptr};
    Scene_root*                                   m_last_hover_scene_item_tree{nullptr};
};

}
