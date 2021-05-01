#ifndef multi_shared_hpp_erhe_graphics
#define multi_shared_hpp_erhe_graphics

namespace erhe::graphics
{

template <typename T>
class Multi_shared
{
public:
    static constexpr size_t entry_count{4};

    template <typename... Args>
    Multi_shared(Args& ... args)
    {
        for (auto& storage : m_storage)
        {
            storage = std::make_shared<T>(args...);
        }
    }

    auto current()
    -> std::shared_ptr<T>
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
    std::array<std::shared_ptr<T>, entry_count> m_storage;
    size_t                                      m_current_entry{0};
};

} // namespace erhe::graphics

#endif
