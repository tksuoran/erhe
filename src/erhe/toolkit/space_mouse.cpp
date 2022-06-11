#include "erhe/toolkit/space_mouse.hpp"
#include "erhe/toolkit/log.hpp"

#include <array>

#pragma comment(lib, "Setupapi.lib")

namespace erhe::toolkit
{

static constexpr uint16_t USB_VENDOR_ID_LOGITECH                = 0x046du;
static constexpr uint16_t USB_VENDOR_ID_3DCONNECTION            = 0x256Fu;
static constexpr uint16_t USB_DEVICE_ID_TRAVELER                = 0xC623u;
static constexpr uint16_t USB_DEVICE_ID_NAVIGATOR               = 0xC626u;
static constexpr uint16_t USB_DEVICE_ID_NAVIGATOR_FOR_NOTEBOOKS = 0xc628u;
static constexpr uint16_t USB_DEVICE_ID_SPACEEXPLORER           = 0xc627u;
static constexpr uint16_t USB_DEVICE_ID_SPACEMOUSE              = 0xC603u;
static constexpr uint16_t USB_DEVICE_ID_SPACEMOUSEPRO           = 0xC62Bu;
static constexpr uint16_t USB_DEVICE_ID_SPACEBALL5000           = 0xc621u;
static constexpr uint16_t USB_DEVICE_ID_SPACEPILOT              = 0xc625u;
static constexpr uint16_t USB_DEVICE_ID_SPACEPILOTPRO           = 0xc629u;

Space_mouse_controller::Space_mouse_controller(Space_mouse_listener& listener)
    : m_listener{listener}
{
    initialize();

    if (m_device != nullptr)
    {
        if (!m_listener.is_active() && !m_running)
        {
            m_thread = std::make_unique<std::thread>(&Space_mouse_controller::run, this);
        }
    }
}

Space_mouse_controller::~Space_mouse_controller()
{
    m_listener.set_active(false);

    if (m_thread)
    {
        if (m_thread->joinable())
        {
            m_thread->join();
        }
    }
}

auto Space_mouse_controller::initialize() -> bool
{
    hid_device_info* const device_list = hid_enumerate(0x0u, 0x0u);
    hid_device_info* device = device_list;

    while (device)
    {
        if (
            (
                device->vendor_id == USB_VENDOR_ID_LOGITECH ||
                device->vendor_id == USB_VENDOR_ID_3DCONNECTION
            ) &&
            (
                device->product_id == USB_DEVICE_ID_TRAVELER                ||
                device->product_id == USB_DEVICE_ID_NAVIGATOR               ||
                device->product_id == USB_DEVICE_ID_NAVIGATOR_FOR_NOTEBOOKS ||
                device->product_id == USB_DEVICE_ID_SPACEEXPLORER           ||
                device->product_id == USB_DEVICE_ID_SPACEMOUSE              ||
                device->product_id == USB_DEVICE_ID_SPACEMOUSEPRO           ||
                device->product_id == USB_DEVICE_ID_SPACEBALL5000           ||
                device->product_id == USB_DEVICE_ID_SPACEPILOT              ||
                device->product_id == USB_DEVICE_ID_SPACEPILOTPRO
            )
        )
        {
            log_space_mouse->trace(
                "3D mouse: vendor = {:04x}, product = {:04x}",
                device->vendor_id,
                device->product_id
            );
            m_device = hid_open(device->vendor_id, device->product_id, nullptr);
            if (m_device != nullptr)
            {
                log_space_mouse->warn(
                    "3D mouse: vendor = {:04x}, product = {:04x} open failed",
                    device->vendor_id,
                    device->product_id
                );
                break;
            }
        }

        device = device->next;
    }
    hid_free_enumeration(device_list);

    if (m_device == nullptr)
    {
        log_space_mouse->info("3D mouse: no supported device found");
        return false;
    }

    return true;
}

auto remap(const unsigned char first, const unsigned char val) -> int
{
    switch (val)
    {
        case   0u: return first;
        case   1u: return first + 255;
        case 254u: return -512 + first;
        case 255u: return -255 + first;
        default:
            return 0;
    }
}

void Space_mouse_controller::run()
{
    m_running = true;

    m_listener.set_active(true);

    while (m_listener.is_active())
    {
        std::array<unsigned char, 7> data{};
        const int result = hid_read_timeout(m_device, data.data(), data.size(), 400); //when timeout
        if (result < 0)
        {
            log_space_mouse->error("3D mouse: read() error");
            stop();
        }
        if (result > 0)
        {
            constexpr const unsigned char OP_TRANSLATION = 1;
            constexpr const unsigned char OP_ROTATION    = 2;
            constexpr const unsigned char OP_BUTTON      = 3;
            switch (data[0])
            {
                case OP_TRANSLATION:
                {
                    const int tx = remap((data[1] & 0x000000ff), (data[2]));
                    const int ty = remap((data[3] & 0x000000ff), (data[4]));
                    const int tz = remap((data[5] & 0x000000ff), (data[6]));
                    m_listener.on_translation(tx, ty, tz);
                    break;
                }

                case OP_ROTATION:
                {
                    const int rx = remap((data[1] & 0x000000ff), (data[2]));
                    const int ry = remap((data[3] & 0x000000ff), (data[4]));
                    const int rz = remap((data[5] & 0x000000ff), (data[6]));
                    m_listener.on_rotation(rx, ry, rz);
                    break;
                }

                case OP_BUTTON:
                {
                    m_listener.on_button(data[1]);
                    break;
                }

                default:
                {
                    break;
                }
            }
        }
    }
    m_running = false;
    stop();
}

void Space_mouse_controller::stop()
{
    if (m_listener.is_active())
    {
        m_listener.set_active(false);

        if ((m_device != nullptr) && m_running)
        {
            hid_close(m_device);
            m_device = nullptr;
            hid_exit();
        }
    }
}

}
