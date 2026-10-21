#include "erhe_graphics/metal/metal_gpu_clock_monitor.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#import <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>

#include <dlfcn.h>

#include <algorithm>
#include <string>

namespace erhe::graphics {

namespace {

using IOReportSubscriptionRef = struct IOReportSubscription*;

using Copy_channels_in_group_fn = CFDictionaryRef         (*)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
using Create_subscription_fn    = IOReportSubscriptionRef (*)(void*, CFMutableDictionaryRef, CFMutableDictionaryRef*, uint64_t, CFTypeRef);
using Create_samples_fn         = CFDictionaryRef         (*)(IOReportSubscriptionRef, CFMutableDictionaryRef, CFTypeRef);
using Create_samples_delta_fn   = CFDictionaryRef         (*)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
using Channel_get_name_fn       = CFStringRef             (*)(CFDictionaryRef);
using State_get_count_fn        = int32_t                 (*)(CFDictionaryRef);
using State_get_name_fn         = CFStringRef             (*)(CFDictionaryRef, int32_t);
using State_get_residency_fn    = int64_t                 (*)(CFDictionaryRef, int32_t);

// The DVFS frequency table of the GPU: (frequency in Hz, voltage in mV)
// pairs, the first the off state.
auto read_gpu_dvfs_mhz() -> std::vector<double>
{
    std::vector<double> result;
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceNameMatching("pmgr"), &iterator) != KERN_SUCCESS) {
        return result;
    }
    for (io_object_t service = IOIteratorNext(iterator); service != IO_OBJECT_NULL; service = IOIteratorNext(iterator)) {
        CFTypeRef property = IORegistryEntryCreateCFProperty(service, CFSTR("voltage-states9"), kCFAllocatorDefault, 0);
        if ((property != nullptr) && (CFGetTypeID(property) == CFDataGetTypeID()) && result.empty()) {
            const CFDataRef      data  = static_cast<CFDataRef>(property);
            const std::uint32_t* words = reinterpret_cast<const std::uint32_t*>(CFDataGetBytePtr(data));
            const CFIndex        pairs = CFDataGetLength(data) / 8;
            for (CFIndex i = 0; i < pairs; ++i) {
                result.push_back(static_cast<double>(words[2 * i]) / 1.0e6);
            }
        }
        if (property != nullptr) {
            CFRelease(property);
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return result;
}

} // anonymous namespace

class Gpu_clock_monitor::Ioreport
{
public:
    Copy_channels_in_group_fn copy_channels_in_group{nullptr};
    Create_subscription_fn    create_subscription   {nullptr};
    Create_samples_fn         create_samples        {nullptr};
    Create_samples_delta_fn   create_samples_delta  {nullptr};
    Channel_get_name_fn       channel_get_name      {nullptr};
    State_get_count_fn        state_get_count       {nullptr};
    State_get_name_fn         state_get_name        {nullptr};
    State_get_residency_fn    state_get_residency   {nullptr};
    void*                     library     {nullptr};
    IOReportSubscriptionRef   subscription{nullptr};
    CFMutableDictionaryRef    channels    {nullptr};
    CFMutableDictionaryRef    subscribed  {nullptr};

    ~Ioreport() noexcept
    {
        if (subscribed != nullptr) {
            CFRelease(subscribed);
        }
        if (channels != nullptr) {
            CFRelease(channels);
        }
        // The library stays loaded: the subscription object is not released
        // through a public symbol, and unloading a dyld-cache image is a no-op.
    }

    // The GPUPH channel of a sample delta, or nullptr.
    auto find_gpuph(const CFDictionaryRef delta) const -> CFDictionaryRef
    {
        const CFArrayRef items = static_cast<CFArrayRef>(CFDictionaryGetValue(delta, CFSTR("IOReportChannels")));
        if (items == nullptr) {
            return nullptr;
        }
        for (CFIndex i = 0, count = CFArrayGetCount(items); i < count; ++i) {
            const CFDictionaryRef item = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(items, i));
            const CFStringRef     name = channel_get_name(item);
            if ((name != nullptr) && (CFStringCompare(name, CFSTR("GPUPH"), 0) == kCFCompareEqualTo)) {
                return item;
            }
        }
        return nullptr;
    }
};

Gpu_clock_monitor::Gpu_clock_monitor(erhe::frame_pacing::Frame_time_recorder& recorder)
    : m_recorder{&recorder}
{
    const auto unavailable = [](const char* reason) {
        log_startup->info("GPU clock monitor unavailable: {}; the frame pacer judges upshifts by GPU spans", reason);
    };
    std::unique_ptr<Ioreport> ioreport = std::make_unique<Ioreport>();
    ioreport->library = dlopen("/usr/lib/libIOReport.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (ioreport->library == nullptr) {
        unavailable("libIOReport not found");
        return;
    }
    ioreport->copy_channels_in_group = reinterpret_cast<Copy_channels_in_group_fn>(dlsym(ioreport->library, "IOReportCopyChannelsInGroup"));
    ioreport->create_subscription    = reinterpret_cast<Create_subscription_fn   >(dlsym(ioreport->library, "IOReportCreateSubscription"));
    ioreport->create_samples         = reinterpret_cast<Create_samples_fn        >(dlsym(ioreport->library, "IOReportCreateSamples"));
    ioreport->create_samples_delta   = reinterpret_cast<Create_samples_delta_fn  >(dlsym(ioreport->library, "IOReportCreateSamplesDelta"));
    ioreport->channel_get_name       = reinterpret_cast<Channel_get_name_fn      >(dlsym(ioreport->library, "IOReportChannelGetChannelName"));
    ioreport->state_get_count        = reinterpret_cast<State_get_count_fn       >(dlsym(ioreport->library, "IOReportStateGetCount"));
    ioreport->state_get_name         = reinterpret_cast<State_get_name_fn        >(dlsym(ioreport->library, "IOReportStateGetNameForIndex"));
    ioreport->state_get_residency    = reinterpret_cast<State_get_residency_fn   >(dlsym(ioreport->library, "IOReportStateGetResidency"));
    if (
        (ioreport->copy_channels_in_group == nullptr) || (ioreport->create_subscription == nullptr) ||
        (ioreport->create_samples == nullptr) || (ioreport->create_samples_delta == nullptr) ||
        (ioreport->channel_get_name == nullptr) || (ioreport->state_get_count == nullptr) ||
        (ioreport->state_get_name == nullptr) || (ioreport->state_get_residency == nullptr)
    ) {
        unavailable("an IOReport symbol is missing");
        return;
    }

    const std::vector<double> dvfs_mhz = read_gpu_dvfs_mhz();
    if (dvfs_mhz.size() < 2) {
        unavailable("no GPU DVFS table (pmgr voltage-states9)");
        return;
    }

    const CFDictionaryRef group = ioreport->copy_channels_in_group(CFSTR("GPU Stats"), CFSTR("GPU Performance States"), 0, 0, 0);
    if (group == nullptr) {
        unavailable("no IOReport GPU Performance States channels");
        return;
    }
    ioreport->channels = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, CFDictionaryGetCount(group), group);
    CFRelease(group);
    ioreport->subscription = ioreport->create_subscription(nullptr, ioreport->channels, &ioreport->subscribed, 0, nullptr);
    if (ioreport->subscription == nullptr) {
        unavailable("IOReport subscription refused");
        return;
    }

    // Map the GPUPH states onto the DVFS table: OFF and P1 ... Pn, state i
    // at table entry i.
    const CFDictionaryRef first = ioreport->create_samples(ioreport->subscription, ioreport->subscribed, nullptr);
    if (first == nullptr) {
        unavailable("IOReport sample failed");
        return;
    }
    const CFDictionaryRef second = ioreport->create_samples(ioreport->subscription, ioreport->subscribed, nullptr);
    const CFDictionaryRef delta  = (second != nullptr) ? ioreport->create_samples_delta(first, second, nullptr) : nullptr;
    const CFDictionaryRef gpuph  = (delta != nullptr) ? ioreport->find_gpuph(delta) : nullptr;
    const int32_t         states = (gpuph != nullptr) ? ioreport->state_get_count(gpuph) : 0;
    std::string           state_names;
    if (static_cast<std::size_t>(states) == dvfs_mhz.size()) {
        m_state_mhz.resize(static_cast<std::size_t>(states), 0.0);
        for (int32_t i = 0; i < states; ++i) {
            const CFStringRef name   = ioreport->state_get_name(gpuph, i);
            const bool        active = (name != nullptr) && (CFStringGetLength(name) > 1) && (CFStringGetCharacterAtIndex(name, 0) == 'P');
            if (active) {
                m_state_mhz[static_cast<std::size_t>(i)] = dvfs_mhz[static_cast<std::size_t>(i)];
                m_max_mhz = std::max(m_max_mhz, dvfs_mhz[static_cast<std::size_t>(i)]);
            }
        }
    }
    CFRelease(first);
    if (second != nullptr) {
        CFRelease(second);
    }
    if (delta != nullptr) {
        CFRelease(delta);
    }
    if (m_max_mhz <= 0.0) {
        log_startup->info("GPU clock monitor: {} GPUPH states against {} DVFS entries", states, dvfs_mhz.size());
        unavailable("GPU performance states do not match the DVFS table");
        m_state_mhz.clear();
        return;
    }
    log_startup->info("GPU clock monitor: IOReport GPUPH, {} states, maximum {:.0f} MHz", states, m_max_mhz);
    m_ioreport = std::move(ioreport);
    m_thread   = std::thread{[this]() { sampler_loop(); }};
}

Gpu_clock_monitor::~Gpu_clock_monitor() noexcept
{
    detach();
}

auto Gpu_clock_monitor::is_available() const -> bool
{
    return m_ioreport != nullptr;
}

void Gpu_clock_monitor::on_frame_gpu_done(const std::int64_t frame_id, const double gpu_begin, const double gpu_end)
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_stop || (m_recorder == nullptr)) {
            return;
        }
        m_pending.push_back(Pending_frame{.frame_id = frame_id, .gpu_begin = gpu_begin, .gpu_end = gpu_end});
    }
    m_condition.notify_one();
}

void Gpu_clock_monitor::detach()
{
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_stop     = true;
        m_recorder = nullptr;
    }
    m_condition.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Gpu_clock_monitor::sampler_loop()
{
    Ioreport&       ioreport = *m_ioreport;
    CFDictionaryRef previous = ioreport.create_samples(ioreport.subscription, ioreport.subscribed, nullptr);
    std::vector<Pending_frame> frames;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock{m_mutex};
            m_condition.wait(lock, [this]() { return m_stop || !m_pending.empty(); });
            if (m_stop) {
                break;
            }
            frames.assign(m_pending.begin(), m_pending.end());
            m_pending.clear();
        }

        // Average active clock since the previous sample - the interval that
        // holds these frames' GPU work - over the maximum DVFS clock.
        double                ratio   = 0.0;
        const CFDictionaryRef current = ioreport.create_samples(ioreport.subscription, ioreport.subscribed, nullptr);
        if ((current != nullptr) && (previous != nullptr)) {
            const CFDictionaryRef delta = ioreport.create_samples_delta(previous, current, nullptr);
            const CFDictionaryRef gpuph = (delta != nullptr) ? ioreport.find_gpuph(delta) : nullptr;
            if (gpuph != nullptr) {
                const int32_t states  = ioreport.state_get_count(gpuph);
                double        active  = 0.0;
                double        weighed = 0.0;
                for (int32_t i = 0; (i < states) && (static_cast<std::size_t>(i) < m_state_mhz.size()); ++i) {
                    const double mhz = m_state_mhz[static_cast<std::size_t>(i)];
                    if (mhz <= 0.0) {
                        continue;
                    }
                    const double residency = static_cast<double>(ioreport.state_get_residency(gpuph, i));
                    active  += residency;
                    weighed += residency * mhz;
                }
                if (active > 0.0) {
                    ratio = (weighed / active) / m_max_mhz;
                }
            }
            if (delta != nullptr) {
                CFRelease(delta);
            }
        }
        if (previous != nullptr) {
            CFRelease(previous);
        }
        previous = current;

        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_recorder == nullptr) {
            break;
        }
        for (const Pending_frame& frame : frames) {
            m_recorder->set(frame.frame_id, &erhe::frame_pacing::Frame_time_record::gpu_clock_ratio, ratio);
            m_recorder->set(
                frame.frame_id,
                &erhe::frame_pacing::Frame_time_record::gpu_frame_begin, frame.gpu_begin,
                &erhe::frame_pacing::Frame_time_record::gpu_frame_end,   frame.gpu_end
            );
        }
    }
    if (previous != nullptr) {
        CFRelease(previous);
    }
}

} // namespace erhe::graphics
