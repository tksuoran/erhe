#pragma once

#include "erhe_graphics/enums.hpp"
#include "erhe_utility/debug_label.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace erhe::graphics {

class Buffer;
class Command_encoder;
class Device;

class Multi_copy_buffer_create_info
{
public:
    // What one copy is expected to hold. The buffer grows past it on demand,
    // so this only decides how much is reserved up front.
    std::size_t                 initial_copy_byte_count{0};
    // 0 = Device::get_number_of_frames_in_flight() + 1, which is the sizing
    // rule (see the class comment). Pass a value only to pin it in a test.
    std::size_t                 copy_count             {0};
    Buffer_target               buffer_target          {};
    std::optional<unsigned int> binding_point          {};
    erhe::utility::Debug_label  debug_label            {};
};

// Storage for a payload that is written when its owner has something new to
// say and re-bound unchanged on every frame in between
// (doc/erhe/draw_list_material_set.md D9).
//
// It holds N complete copies of the payload and keeps one of them current.
// begin_write() hands out a copy that no unretired frame is reading, commit()
// makes it the current one, and bind() binds whichever copy is current. A
// frame that writes nothing simply binds the copy that is already there.
//
// Sizing: with F frames in flight, at most F frames are unretired and each
// bound exactly one copy, so at most F copies can be unsafe to write; F + 1
// copies therefore always leave one free, even when every consecutive frame
// writes.
//
// Deliberately not a Ring_buffer_client: the device's ring buffers are shared
// per Buffer_target and circular, so a range held for hundreds of frames would
// sit in the middle of a FIFO every other client allocates from and stall
// reclamation behind it.
class Multi_copy_buffer final
{
public:
    Multi_copy_buffer(Device& device, const Multi_copy_buffer_create_info& create_info);
    ~Multi_copy_buffer() noexcept;
    Multi_copy_buffer (const Multi_copy_buffer&) = delete;
    void operator=    (const Multi_copy_buffer&) = delete;
    Multi_copy_buffer (Multi_copy_buffer&&)      = delete;
    void operator=    (Multi_copy_buffer&&)      = delete;

    // A writable span of byte_count bytes in a copy other than the current
    // one, chosen so that no unretired frame is reading it. Reallocates when
    // byte_count exceeds the current copy size, and also when every other copy
    // is still in use - growing rather than stalling or overwriting, because
    // the backends answer "is this frame retired?" conservatively and a
    // consumer must not be wedged by a watermark that lags.
    //
    // Must be followed by commit() (or by nothing at all, which abandons the
    // write and leaves the current copy untouched).
    [[nodiscard]] auto begin_write(std::size_t byte_count) -> std::span<std::byte>;

    // The written copy becomes current. The previous one stays intact until a
    // later begin_write() picks it, which cannot happen while a frame that
    // bound it is unretired.
    void commit(std::size_t byte_count);

    // Binds the current copy and stamps it as used by the current device
    // frame. False when nothing has been committed yet.
    auto bind(Command_encoder& command_encoder) -> bool;

    [[nodiscard]] auto has_current            () const -> bool;
    [[nodiscard]] auto get_current_byte_count () const -> std::size_t;
    [[nodiscard]] auto get_current_byte_offset() const -> std::size_t;
    [[nodiscard]] auto get_copy_byte_count    () const -> std::size_t;
    [[nodiscard]] auto get_copy_count         () const -> std::size_t;
    // Observability, for "a clean frame writes nothing" tests and for the
    // before/after measurement the persistence work exists for.
    [[nodiscard]] auto get_commit_count       () const -> std::size_t;
    // How many allocations this buffer has made, including the first: one more
    // than the number of times it grew or ran out of free copies.
    [[nodiscard]] auto get_allocation_count   () const -> std::size_t;
    [[nodiscard]] auto get_retired_count      () const -> std::size_t;

private:
    class Copy
    {
    public:
        std::size_t byte_offset    {0};
        std::size_t byte_count     {0};
        // Refreshed by bind(), NOT by commit(): a copy committed once and then
        // bound for hundreds of frames is in use on every one of them.
        uint64_t    last_used_frame{0};
        bool        used           {false};  // last_used_frame is meaningful
        bool        written        {false};
    };

    // Replaces the allocation with one sized for copy_byte_count, retiring the
    // old one until every frame that bound part of it has completed. Nothing
    // is copied forward: the caller writes a complete payload immediately.
    void reallocate         (std::size_t copy_byte_count);
    void release_retired    ();
    [[nodiscard]] auto find_free_copy() const -> std::size_t;

    static constexpr std::size_t invalid_index = static_cast<std::size_t>(-1);

    Device&                     m_device;
    Buffer_target               m_buffer_target;
    std::optional<unsigned int> m_binding_point;
    erhe::utility::Debug_label  m_debug_label;
    std::size_t                 m_alignment       {1};
    std::size_t                 m_copy_byte_count {0};
    std::unique_ptr<Buffer>     m_buffer;
    std::vector<Copy>           m_copies;
    // Superseded allocations with the highest frame that bound any of their
    // copies; destroyed once that frame is retired.
    std::vector<std::pair<std::unique_ptr<Buffer>, uint64_t>> m_retired;
    // CPU staging for the path where the buffer has no persistent map; unused
    // (and empty) when writes go straight through the map.
    std::vector<std::byte>      m_staging;
    bool                        m_staging_in_use  {false};
    std::size_t                 m_current         {invalid_index};
    std::size_t                 m_writing         {invalid_index};
    std::size_t                 m_writing_bytes   {0};
    std::size_t                 m_commit_count    {0};
    std::size_t                 m_allocation_count{0};
};

} // namespace erhe::graphics
