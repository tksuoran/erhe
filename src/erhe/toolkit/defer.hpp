#pragma once

#include <type_traits>

namespace erhe::toolkit
{

template<typename Callable>
class Deferral
{
public:
    template<typename Parameter_callable>
    Deferral(Parameter_callable&& callable)
        : m_callable(static_cast<Parameter_callable&&>(callable))
    {
    }

    Deferral(const Deferral&) = delete;
    Deferral& operator=(const Deferral&) = delete;

    ~Deferral()
    {
        m_callable();
    }

private:
    Callable m_callable;
};

class Deferral_helper
{
public:
    template<typename Callable>
    Deferral<
        typename std::remove_cv<
            typename std::remove_reference<Callable>::type
        >::type
    > operator->*(Callable&& callable)
    {
        return static_cast<Callable&&>(callable);
    }
};

} // namespace erhe::toolkit

#define ERHE_CAT_(a, b) a ## b
#define ERHE_CAT(a, b) ERHE_CAT_(a, b)

#define ERHE_DEFER \
    auto const ERHE_CAT(ERHE_DEFER_,__COUNTER__) = erhe::toolkit::Deferral_helper{} ->* [&]() -> void
