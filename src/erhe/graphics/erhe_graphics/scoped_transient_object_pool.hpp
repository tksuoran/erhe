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
// Pools are a per-thread stack: one is drained on the thread that opened
// it, in reverse opening order. A pool therefore never spans a device frame
// (frames overlap, and with a render thread a frame is acquired on one
// thread and ended on another).
//
// The Metal backend opens one around each call that creates such objects
// and takes its own reference to the ones it keeps (command buffers,
// drawables, compute and render command encoders), so their lifetime does
// not depend on the caller's pools. A thread that calls into the device
// repeatedly still opens one per loop iteration (the editor's main loop and
// render thread) to drain whatever else the driver autoreleases there; code
// that submits outside such a loop (glTF texture uploads on a loader
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
