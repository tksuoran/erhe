#include "erhe_graphics/metal/metal_clock_monitor.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#import <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>

#include <dlfcn.h>

#include <algorithm>

namespace erhe::graphics {

namespace {

using IOReportSubscriptionRef = struct IOReportSubscription*;

using Copy_channels_in_group_fn = CFDictionaryRef         (*)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
using Merge_channels_fn         = void                    (*)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
using Create_subscription_fn    = IOReportSubscriptionRef (*)(void*, CFMutableDictionaryRef, CFMutableDictionaryRef*, uint64_t, CFTypeRef);
using Create_samples_fn         = CFDictionaryRef         (*)(IOReportSubscriptionRef, CFMutableDictionaryRef, CFTypeRef);
using Create_samples_delta_fn   = CFDictionaryRef         (*)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
using Channel_get_name_fn       = CFStringRef             (*)(CFDictionaryRef);
using State_get_count_fn        = int32_t                 (*)(CFDictionaryRef);
using State_get_name_fn         = CFStringRef             (*)(CFDictionaryRef, int32_t);
using State_get_residency_fn    = int64_t                 (*)(CFDictionaryRef, int32_t);

// A DVFS frequency table of the pmgr IORegistry entry: (frequency, voltage)
// pairs of 32-bit words, the frequency in units of 1 / units_per_mhz MHz.
auto read_dvfs_mhz(const CFStringRef key, const double units_per_mhz) -> std::vector<double>
{
    std::vector<double> result;
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceNameMatching("pmgr"), &iterator) != KERN_SUCCESS) {
        return result;
    }
    for (io_object_t service = IOIteratorNext(iterator); service != IO_OBJECT_NULL; service = IOIteratorNext(iterator)) {
        CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
        if ((property != nullptr) && (CFGetTypeID(property) == CFDataGetTypeID()) && result.empty()) {
            const CFDataRef      data  = static_cast<CFDataRef>(property);
            const std::uint32_t* words = reinterpret_cast<const std::uint32_t*>(CFDataGetBytePtr(data));
            const CFIndex        pairs = CFDataGetLength(data) / 8;
            for (CFIndex i = 0; i < pairs; ++i) {
                result.push_back(static_cast<double>(words[2 * i]) / units_per_mhz);
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

auto to_string(const CFStringRef string) -> std::string
{
    if (string == nullptr) {
        return {};
    }
    char buffer[128]{};
    if (!CFStringGetCString(string, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return {};
    }
    return std::string{buffer};
}

} // anonymous namespace

class Clock_monitor::Ioreport
{
public:
    Copy_channels_in_group_fn copy_channels_in_group{nullptr};
    Merge_channels_fn         merge_channels        {nullptr};
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
        // The library stays loaded: the subscription object has no public
        // release, and unloading a dyld-cache image is a no-op.
    }

    // Calls visit(name, item) for each channel of a sample delta.
    template <typename Visit>
    void for_each_channel(const CFDictionaryRef delta, Visit&& visit) const
    {
        const CFArrayRef items = static_cast<CFArrayRef>(CFDictionaryGetValue(delta, CFSTR("IOReportChannels")));
        if (items == nullptr) {
            return;
        }
        for (CFIndex i = 0, count = CFArrayGetCount(items); i < count; ++i) {
            const CFDictionaryRef item = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(items, i));
            visit(to_string(channel_get_name(item)), item);
        }
    }

    // Residency-weighted average active clock of a domain's channels in a
    // sample delta, over the domain's maximum; 0 without active residency.
    auto clock_ratio(const CFDictionaryRef delta, const Domain& domain) const -> double
    {
        if (domain.max_mhz <= 0.0) {
            return 0.0;
        }
        double active  = 0.0;
        double weighed = 0.0;
        for_each_channel(delta, [&](const std::string& name, const CFDictionaryRef item) {
            for (const Channel& channel : domain.channels) {
                if (channel.name != name) {
                    continue;
                }
                const int32_t states = state_get_count(item);
                for (int32_t i = 0; (i < states) && (static_cast<std::size_t>(i) < channel.state_mhz.size()); ++i) {
                    const double mhz = channel.state_mhz[static_cast<std::size_t>(i)];
                    if (mhz <= 0.0) {
                        continue;
                    }
                    const double residency = static_cast<double>(state_get_residency(item, i));
                    active  += residency;
                    weighed += residency * mhz;
                }
            }
        });
        return (active > 0.0) ? ((weighed / active) / domain.max_mhz) : 0.0;
    }
};

Clock_monitor::Clock_monitor(erhe::frame_pacing::Frame_time_recorder& recorder)
    : m_recorder{&recorder}
{
    const auto unavailable = [](const char* reason) {
        log_startup->info("clock monitor unavailable: {}; the frame pacer judges upshifts by stage times", reason);
    };
    std::unique_ptr<Ioreport> ioreport = std::make_unique<Ioreport>();
    ioreport->library = dlopen("/usr/lib/libIOReport.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (ioreport->library == nullptr) {
        unavailable("libIOReport not found");
        return;
    }
    ioreport->copy_channels_in_group = reinterpret_cast<Copy_channels_in_group_fn>(dlsym(ioreport->library, "IOReportCopyChannelsInGroup"));
    ioreport->merge_channels         = reinterpret_cast<Merge_channels_fn        >(dlsym(ioreport->library, "IOReportMergeChannels"));
    ioreport->create_subscription    = reinterpret_cast<Create_subscription_fn   >(dlsym(ioreport->library, "IOReportCreateSubscription"));
    ioreport->create_samples         = reinterpret_cast<Create_samples_fn        >(dlsym(ioreport->library, "IOReportCreateSamples"));
    ioreport->create_samples_delta   = reinterpret_cast<Create_samples_delta_fn  >(dlsym(ioreport->library, "IOReportCreateSamplesDelta"));
    ioreport->channel_get_name       = reinterpret_cast<Channel_get_name_fn      >(dlsym(ioreport->library, "IOReportChannelGetChannelName"));
    ioreport->state_get_count        = reinterpret_cast<State_get_count_fn       >(dlsym(ioreport->library, "IOReportStateGetCount"));
    ioreport->state_get_name         = reinterpret_cast<State_get_name_fn        >(dlsym(ioreport->library, "IOReportStateGetNameForIndex"));
    ioreport->state_get_residency    = reinterpret_cast<State_get_residency_fn   >(dlsym(ioreport->library, "IOReportStateGetResidency"));
    if (
        (ioreport->copy_channels_in_group == nullptr) || (ioreport->merge_channels == nullptr) ||
        (ioreport->create_subscription == nullptr) || (ioreport->create_samples == nullptr) ||
        (ioreport->create_samples_delta == nullptr) || (ioreport->channel_get_name == nullptr) ||
        (ioreport->state_get_count == nullptr) || (ioreport->state_get_name == nullptr) ||
        (ioreport->state_get_residency == nullptr)
    ) {
        unavailable("an IOReport symbol is missing");
        return;
    }

    const CFDictionaryRef gpu_group = ioreport->copy_channels_in_group(CFSTR("GPU Stats"), CFSTR("GPU Performance States"), 0, 0, 0);
    const CFDictionaryRef cpu_group = ioreport->copy_channels_in_group(CFSTR("CPU Stats"), CFSTR("CPU Core Performance States"), 0, 0, 0);
    if (gpu_group == nullptr) {
        if (cpu_group != nullptr) {
            CFRelease(cpu_group);
        }
        unavailable("no IOReport GPU Performance States channels");
        return;
    }
    ioreport->channels = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, gpu_group);
    CFRelease(gpu_group);
    if (cpu_group != nullptr) {
        ioreport->merge_channels(ioreport->channels, cpu_group, nullptr);
        CFRelease(cpu_group);
    }
    ioreport->subscription = ioreport->create_subscription(nullptr, ioreport->channels, &ioreport->subscribed, 0, nullptr);
    if (ioreport->subscription == nullptr) {
        unavailable("IOReport subscription refused");
        return;
    }

    // Map the channels' states onto the DVFS tables. GPUPH: OFF and P1 ...
    // Pn, state i at GPU table entry i. PCPU<cluster><core>0: DOWN, IDLE and
    // V0P<n> ... V<n>P0, state i at the cluster's table entry i - 2.
    const std::vector<double> gpu_mhz   = read_dvfs_mhz(CFSTR("voltage-states9"), 1.0e6);
    const std::vector<double> pcpu0_mhz = read_dvfs_mhz(CFSTR("voltage-states5-sram"), 1.0e3);
    const std::vector<double> pcpu1_mhz = read_dvfs_mhz(CFSTR("voltage-states13-sram"), 1.0e3);
    const CFDictionaryRef first  = ioreport->create_samples(ioreport->subscription, ioreport->subscribed, nullptr);
    const CFDictionaryRef second = (first != nullptr) ? ioreport->create_samples(ioreport->subscription, ioreport->subscribed, nullptr) : nullptr;
    const CFDictionaryRef delta  = (second != nullptr) ? ioreport->create_samples_delta(first, second, nullptr) : nullptr;
    if (delta != nullptr) {
        ioreport->for_each_channel(delta, [&](const std::string& name, const CFDictionaryRef item) {
            const int32_t states = ioreport->state_get_count(item);
            if (states <= 0) {
                return;
            }
            if (name == "GPUPH") {
                if (static_cast<std::size_t>(states) != gpu_mhz.size()) {
                    log_startup->info("clock monitor: GPUPH has {} states, the GPU DVFS table {} entries", states, gpu_mhz.size());
                    return;
                }
                Channel channel{.name = name, .state_mhz = std::vector<double>(static_cast<std::size_t>(states), 0.0)};
                for (int32_t i = 0; i < states; ++i) {
                    const std::string state = to_string(ioreport->state_get_name(item, i));
                    if ((state.size() > 1) && (state[0] == 'P')) {
                        channel.state_mhz[static_cast<std::size_t>(i)] = gpu_mhz[static_cast<std::size_t>(i)];
                        m_gpu.max_mhz = std::max(m_gpu.max_mhz, gpu_mhz[static_cast<std::size_t>(i)]);
                    }
                }
                m_gpu.channels.push_back(std::move(channel));
            } else if ((name.size() == 7) && (name.compare(0, 4, "PCPU") == 0)) {
                const std::vector<double>& table = (name[4] == '0') ? pcpu0_mhz : pcpu1_mhz;
                if (static_cast<std::size_t>(states) != (table.size() + 2)) {
                    return;
                }
                Channel channel{.name = name, .state_mhz = std::vector<double>(static_cast<std::size_t>(states), 0.0)};
                for (int32_t i = 2; i < states; ++i) {
                    channel.state_mhz[static_cast<std::size_t>(i)] = table[static_cast<std::size_t>(i - 2)];
                    m_cpu.max_mhz = std::max(m_cpu.max_mhz, table[static_cast<std::size_t>(i - 2)]);
                }
                m_cpu.channels.push_back(std::move(channel));
            }
        });
        CFRelease(delta);
    }
    if (first != nullptr) {
        CFRelease(first);
    }
    if (second != nullptr) {
        CFRelease(second);
    }
    if (m_gpu.max_mhz <= 0.0) {
        unavailable("the GPU performance states do not match the GPU DVFS table");
        return;
    }
    if (m_cpu.max_mhz <= 0.0) {
        log_startup->info("clock monitor: no P-core performance states matching their DVFS tables; CPU clock not measured");
    }
    log_startup->info(
        "clock monitor: IOReport GPUPH (maximum {:.0f} MHz), {} P-core channels (maximum {:.0f} MHz)",
        m_gpu.max_mhz, m_cpu.channels.size(), m_cpu.max_mhz
    );
    m_ioreport = std::move(ioreport);
    m_thread   = std::thread{[this]() { sampler_loop(); }};
}

Clock_monitor::~Clock_monitor() noexcept
{
    detach();
}

auto Clock_monitor::is_available() const -> bool
{
    return m_ioreport != nullptr;
}

void Clock_monitor::on_frame_gpu_done(const std::int64_t frame_id, const double gpu_begin, const double gpu_end)
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

void Clock_monitor::detach()
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

void Clock_monitor::sampler_loop()
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

        // Average active clocks since the previous sample - the interval
        // that holds these frames' GPU work - over the maximum DVFS clocks.
        double                gpu_ratio = 0.0;
        double                cpu_ratio = 0.0;
        const CFDictionaryRef current   = ioreport.create_samples(ioreport.subscription, ioreport.subscribed, nullptr);
        if ((current != nullptr) && (previous != nullptr)) {
            const CFDictionaryRef delta = ioreport.create_samples_delta(previous, current, nullptr);
            if (delta != nullptr) {
                gpu_ratio = ioreport.clock_ratio(delta, m_gpu);
                cpu_ratio = ioreport.clock_ratio(delta, m_cpu);
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
            m_recorder->set(
                frame.frame_id,
                &erhe::frame_pacing::Frame_time_record::gpu_clock_ratio, gpu_ratio,
                &erhe::frame_pacing::Frame_time_record::cpu_clock_ratio, cpu_ratio
            );
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
