#if 0
#pragma once

namespace editor
{


class Editor_view_client
    : public IUpdate_fixed_step
{
public:
    Editor_view_client();

    // Implements erhe::components::IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context&) override;

    void update() override;
};

} // namespace editor


#endif
