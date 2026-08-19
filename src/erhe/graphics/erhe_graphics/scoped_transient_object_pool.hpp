#pragma once

namespace erhe::graphics {

// Bounds the lifetime of driver-owned temporary objects created inside the
// scope.
//
// Metal: metal-cpp hands out most transient per-frame objects autoreleased
// (MTL::CommandQueue::commandBuffer, the command encoders, CA::MetalLayer
// nextDrawable) - only alloc/new/copy methods return owned references. With
// no NS::AutoreleasePool in place those objects are never deallocated, and
// MTLCommandQueue recycles its in-flight command buffer slots
// (maxCommandBufferCount, 64 by default) only when a command buffer
// deallocates. Once the slots are gone, the next commandBuffer() blocks on
// the queue's semaphore forever - observed as a hung main thread inside
// -[_MTLCommandBuffer initWithQueue:retainedReferences:] while loading a
// large glTF scene.
//
// So: every thread that records GPU work must have one of these covering it.
// The device frame keeps one open between wait_frame() and end_frame(); code
// that submits outside a device frame (glTF texture uploads on a loader
// thread) opens its own.
//
// Backends other than Metal have no such objects, and this is an empty no-op
// there.
class Scoped_transient_object_pool final
{
public:
    Scoped_transient_object_pool ();
    ~Scoped_transient_object_pool() noexcept;
    Scoped_transient_object_pool (const Scoped_transient_object_pool&) = delete;
    void operator=               (const Scoped_transient_object_pool&) = delete;
    Scoped_transient_object_pool (Scoped_transient_object_pool&&)      = delete;
    void operator=               (Scoped_transient_object_pool&&)      = delete;

private:
    void* m_pool{nullptr};
};

} // namespace erhe::graphics
