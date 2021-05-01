#ifndef multi_hpp_erhe_graphics
#define multi_hpp_erhe_graphics

namespace erhe::graphics
{

template <typename T>
class Multi
{
public:
    static constexpr size_t entry_count{4};

    template <typename... Args>
    Multi(Args ... args)
    {
        for (auto& storage : m_storage)
        {
            storage = T(args...);
        }
    }

    auto current()
    -> T&
    {
        Expects(m_current_entry < m_storage.size());

        return m_storage[m_current_entry];
    }

    void advance()
    {
        m_current_entry = (m_current_entry + 1) % m_storage.size();

        Ensures(m_current_entry < m_storage.size());
    }

private:
    std::array<T, entry_count> m_storage;
    size_t                     m_current_entry{0};
};

} // namespace erhe::graphics

#endif
