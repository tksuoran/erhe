#pragma once

namespace erhe::graph {

class Pin;
class Link;

class Link
{
public:
    Link();
    Link(Link&& old);
    Link(const Link& other) = delete;
    Link& operator=(Link&& old);
    Link& operator=(const Link& other) = delete;
    Link(Pin* source, Pin* sink);
    virtual ~Link();

    [[nodiscard]] auto get_id      () const -> int;
    [[nodiscard]] auto get_source  () const -> Pin*;
    [[nodiscard]] auto get_sink    () const -> Pin*;
    [[nodiscard]] auto is_connected() const -> bool;
    void disconnect();

private:
    int  m_id;
    Pin* m_source{nullptr};
    Pin* m_sink  {nullptr};
};

} // namespace erhe::graph
