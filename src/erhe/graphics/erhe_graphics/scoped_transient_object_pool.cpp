#include "erhe_graphics/scoped_transient_object_pool.hpp"

#if defined(ERHE_GRAPHICS_API_METAL)
#   include <Foundation/Foundation.hpp>
#endif

namespace erhe::graphics {

Scoped_transient_object_pool::Scoped_transient_object_pool()
{
#if defined(ERHE_GRAPHICS_API_METAL)
    m_pool = NS::AutoreleasePool::alloc()->init();
#endif
}

Scoped_transient_object_pool::~Scoped_transient_object_pool() noexcept
{
#if defined(ERHE_GRAPHICS_API_METAL)
    // release() drains the pool: everything autoreleased since the
    // constructor ran is released now. Pools are a per-thread stack, so
    // this must happen on the thread that created it, and in reverse
    // creation order.
    if (m_pool != nullptr) {
        static_cast<NS::AutoreleasePool*>(m_pool)->release();
        m_pool = nullptr;
    }
#endif
}

} // namespace erhe::graphics
