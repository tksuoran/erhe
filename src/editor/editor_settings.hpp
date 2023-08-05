#pragma once

namespace editor {

class Editor_settings
{
public:
    // Node tree
    bool node_tree_expand_attachments{false};
    bool node_tree_show_all          {false};

    // Physics
    bool physics_static_enable {true};
    bool physics_dynamic_enable{true};
};

}
