#ifndef action_hpp_erhe_ui
#define action_hpp_erhe_ui

namespace erhe::ui
{

class Action_sink;

class Action_source
{
public:
    auto sink() const
    -> Action_sink*
    {
        return m_action_sink;
    }

    void set_sink(Action_sink* value)
    {
        m_action_sink = value;
    }

protected:
    Action_source() = default;

    explicit Action_source(Action_sink* sink)
        : m_action_sink{sink}
    {
    }

private:
    Action_sink* m_action_sink{nullptr};
};

class Action_sink
{
public:
    virtual ~Action_sink() = default;

    virtual void action(Action_source* source)
    {
        static_cast<void>(source);
    }
};

} // namespace erhe::ui

#endif
