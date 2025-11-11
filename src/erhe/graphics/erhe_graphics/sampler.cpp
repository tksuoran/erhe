#include "erhe_graphics/sampler.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_sampler.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#endif

#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

Sampler::Sampler(Device& device, const Sampler_create_info& create_info)
    : m_impl{std::make_unique<Sampler_impl>(device, create_info)}
{
}
Sampler::~Sampler() noexcept = default;
auto Sampler::get_debug_label() const -> const std::string&
{
    return m_impl->get_debug_label();
}
auto Sampler::uses_mipmaps() const -> bool
{
    return m_impl->uses_mipmaps();
}
auto Sampler::get_lod_bias() const -> float
{
    return m_impl->get_lod_bias();
}
auto Sampler::get_impl() -> Sampler_impl&
{
    return *m_impl.get();
}
auto Sampler::get_impl() const -> const Sampler_impl&
{
    return *m_impl.get();
}

// auto operator==(const Sampler& lhs, const Sampler& rhs) noexcept -> bool
// {
//     ERHE_VERIFY(lhs.gl_name() != 0);
//     ERHE_VERIFY(rhs.gl_name() != 0);
// 
//     return lhs.gl_name() == rhs.gl_name();
// }
// 
// auto operator!=(const Sampler& lhs, const Sampler& rhs) noexcept -> bool
// {
//     return !(lhs == rhs);
// }

//class Sampler_hash
//{
//public:
//    [[nodiscard]] auto operator()(const Sampler& sampler) const noexcept -> size_t
//    {
//        ERHE_VERIFY(sampler.gl_name() != 0);
//
//        return static_cast<size_t>(sampler.gl_name());
//    }
//};

} // namespace erhe::graphics
