#pragma once

namespace editor {

class Render_context;

class Renderable
{
public:
    virtual ~Renderable() noexcept = default;

    virtual void render(const Render_context& context) = 0;
};

} // namespace editor
