#if 0
#pragma once

#include <mutex>
#include <vector>


namespace erhe::graphics
{

class Buffer;

class Buffer_transfer_queue final
{
public:
    Buffer_transfer_queue ();
    ~Buffer_transfer_queue() noexcept;
    Buffer_transfer_queue (Buffer_transfer_queue&) = delete;
    auto operator=        (Buffer_transfer_queue&) -> Buffer_transfer_queue& = delete;

    class Transfer_entry
    {
    public:
        Transfer_entry(
            Buffer&                target,
            const std::size_t      target_offset,
            std::vector<uint8_t>&& data
        )
            : target       {target}
            , target_offset{target_offset}
            , data         {data}
        {
        }

        Transfer_entry(Transfer_entry&) = delete;
        void operator=(Transfer_entry&) = delete;

        Transfer_entry(Transfer_entry&& other) noexcept
            : target       {other.target}
            , target_offset{other.target_offset}
            , data         {std::move(other.data)}
        {
        }

        auto operator=(Transfer_entry&& other) = delete;

        Buffer&              target;
        std::size_t          target_offset{0};
        std::vector<uint8_t> data;
    };

    void flush();

    void enqueue(
        Buffer&                buffer,
        std::size_t            offset,
        std::vector<uint8_t>&& data
    );

private:
    std::mutex                  m_mutex;
    std::vector<Transfer_entry> m_queued;
};


} // namespace erhe::graphics
#endif