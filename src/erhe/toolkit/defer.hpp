#pragma once

#include <functional>

namespace erhe::toolkit
{

class Defer
{
public:
    explicit Defer(Defer&& rvalue)
    {
        m_callback = std::move(rvalue.m_callback);
        rvalue.m_callback = nullptr;
    }

    Defer() noexcept
    {
    }

    explicit Defer(const std::function<void()>& func) noexcept
    {
        m_callback = func;
    }

    ~Defer()
    {
        m_callback();
    }

    Defer(const Defer& rvalue)          = delete;
    void operator=(const Defer& rvalue) = delete;

    void bind(const std::function<void()>& callback)
    {
        m_callback = callback;
    }

private:
    std::function<void()> m_callback;
};

} // namespace erhe::toolkit

#define ERHE_DEFER_2(fun, count) erhe::toolkit::Defer defer_ ## count(fun)
#define ERHE_DEFER_1(fun, count) ERHE_DEFER_2(fun, count)
#define ERHE_DEFER_FUN(fun) ERHE_DEFER_1(fun, __COUNTER__)
#define ERHE_DEFER(statements) ERHE_DEFER_FUN( [&]() { statements } )

