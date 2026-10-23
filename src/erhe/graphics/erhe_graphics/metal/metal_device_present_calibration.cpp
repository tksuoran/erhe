// Present calibration at device init, Metal part (the algorithm and its
// rationale: present_calibration_runner.cpp): clear-only drawables on the
// window's CAMetalLayer, requested with presentDrawable:atTime:; the
// presented handler gives the displayed time, the completed handler the GPU
// end. The GPU pre-warm kernel rate is measured first.
//
// Runs synchronously on the init thread; presented and completed handlers
// arrive on Metal's own threads and are collected under a mutex.

#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_gpu_prewarm.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/present_calibration.hpp"
#include "erhe_graphics/present_calibration_cache.hpp"
#include "erhe_graphics/present_calibration_runner.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_graphics/surface.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graphics {

namespace {

constexpr std::uint32_t s_prewarm_rate_iterations = 20000;

class Calibration_feedback
{
public:
    using Entry = Present_calibration_probe_feedback;

    auto add() -> std::size_t
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_entries.emplace_back();
        return m_entries.size() - 1;
    }
    void set_presented(const std::size_t index, const double presented_time)
    {
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            m_entries[index].presented_time = presented_time;
            m_entries[index].presented      = true;
        }
        m_condition.notify_all();
    }
    void set_gpu_end(const std::size_t index, const double gpu_end)
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_entries[index].gpu_end = gpu_end;
    }
    // The entry once presented, or as it stands at the timeout.
    auto wait(const std::size_t index, const double timeout_seconds) -> Entry
    {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_condition.wait_for(
            lock,
            std::chrono::duration<double>(std::max(0.0, timeout_seconds)),
            [this, index]() { return m_entries[index].presented; }
        );
        return m_entries[index];
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_condition;
    std::vector<Entry>      m_entries; // handlers index into it under m_mutex
};

// The probes on the window's layer (IPresent_calibration_presenter).
class Metal_calibration_presenter : public IPresent_calibration_presenter
{
public:
    Metal_calibration_presenter(CA::MetalLayer* layer, MTL::CommandQueue* command_queue, Surface_impl& surface_impl)
        : m_layer        {layer}
        , m_command_queue{command_queue}
        , m_surface_impl {surface_impl}
        , m_feedback     {std::make_shared<Calibration_feedback>()}
    {
    }

    auto present(const double request_time, const double commit_time) -> std::ptrdiff_t override
    {
        Scoped_transient_object_pool object_pool{};
        CA::MetalDrawable* const drawable = m_layer->nextDrawable();
        if (drawable == nullptr) {
            return -1;
        }
        MTL::CommandBuffer* const command_buffer = m_command_queue->commandBuffer();
        if (command_buffer == nullptr) {
            return -1;
        }
        command_buffer->setLabel(NS::String::string("present calibration", NS::UTF8StringEncoding));
        MTL::RenderPassDescriptor* const render_pass = MTL::RenderPassDescriptor::renderPassDescriptor();
        MTL::RenderPassColorAttachmentDescriptor* const color = render_pass->colorAttachments()->object(0);
        color->setTexture(drawable->texture());
        color->setLoadAction(MTL::LoadActionClear);
        color->setClearColor(MTL::ClearColor{0.0, 0.0, 0.0, 1.0});
        color->setStoreAction(MTL::StoreActionStore);
        MTL::RenderCommandEncoder* const encoder = command_buffer->renderCommandEncoder(render_pass);
        encoder->endEncoding();

        const std::size_t index = m_feedback->add();
        std::shared_ptr<Calibration_feedback> presented_feedback = m_feedback;
        drawable->addPresentedHandler(
            [presented_feedback, index](MTL::Drawable* presented) {
                presented_feedback->set_presented(index, presented->presentedTime());
            }
        );
        std::shared_ptr<Calibration_feedback> completed_feedback = m_feedback;
        command_buffer->addCompletedHandler(
            [completed_feedback, index](MTL::CommandBuffer* completed) {
                completed_feedback->set_gpu_end(index, completed->GPUEndTime());
            }
        );
        if (request_time > 0.0) {
            command_buffer->presentDrawableAtTime(drawable, request_time);
        } else {
            command_buffer->presentDrawable(drawable);
        }
        if (commit_time > 0.0) {
            present_calibration_wait_until(commit_time);
        }
        command_buffer->commit();
        return static_cast<std::ptrdiff_t>(index);
    }

    auto wait(const std::size_t index, const double timeout_seconds) -> Present_calibration_probe_feedback override
    {
        return m_feedback->wait(index, timeout_seconds);
    }

    void service_window() override
    {
        m_surface_impl.service_main_run_loop();
    }

    // The probe configuration (drawable count, queue depth, the layer's
    // pixel format and display sync) is part of what the calibration
    // measures: a change bumps c_present_calibration_version
    // (present_calibration_cache.hpp).
    auto get_queue_depth() const -> std::size_t override
    {
        return 3; // Surface_impl::apply_frame_pacing_tier: maximumDrawableCount 3
    }

private:
    CA::MetalLayer*                       m_layer;
    MTL::CommandQueue*                    m_command_queue;
    Surface_impl&                         m_surface_impl;
    std::shared_ptr<Calibration_feedback> m_feedback; // shared with the handlers, which may outlive the calibration
};

} // namespace

void Device_impl::run_present_calibration()
{
    Present_calibration result{};
    result.done = true;

    // 0. GPU pre-warm kernel rate (claim C26), first: it does not depend on
    // the compositor, so a calibration that stops early still enables the
    // pre-warm. The fill that raises the clock is learned online by the
    // frame pacer: with clear-only frames the governor stays at its lowest
    // state even under 8 ms of ALU work per frame, while on top of the
    // editor's real frames 2.3 ms raised it to ~0.97 - the governor responds
    // to the real workload.
    if (m_gpu_prewarm != nullptr) {
        for (int i = 0; i < 3; ++i) {
            static_cast<void>(m_gpu_prewarm->run_blocking(s_prewarm_rate_iterations));
        }
        result.prewarm_seconds_per_iteration = m_gpu_prewarm->get_seconds_per_iteration();
        log_display_refresh->info("present calibration: GPU pre-warm kernel {:.1f} ns per iteration", result.prewarm_seconds_per_iteration * 1.0e9);
    }

    // A cached calibration for this display, mode, OS build, GPU and path
    // (doc/erhe/graphics.md "Present calibration cache") replaces the probes.
    if (m_device.begin_present_calibration(m_frame_pacing_window, m_mtl_device->name()->utf8String(), Present_path::metal, result)) {
        return;
    }

    CA::MetalLayer* const layer          = (m_surface != nullptr) ? m_surface->get_impl().get_metal_layer() : nullptr;
    const double          nominal_period = m_device.get_display_refresh_duration_seconds();
    const char*           failure        = nullptr;
    if (layer == nullptr) {
        failure = "no CAMetalLayer";
        result.status = failure;
    } else {
        Metal_calibration_presenter presenter{layer, m_mtl_command_queue, m_surface->get_impl()};
        failure = erhe::graphics::run_present_calibration(presenter, nominal_period, result);
    }
    if (failure != nullptr) {
        log_display_refresh->warn("present calibration: {}; tier M requests at T/4 before the target and estimates the deadline from 2.5 ms", failure);
    } else {
        log_display_refresh->info("present calibration: {}", result.status);
    }
    m_device.finish_present_calibration(result);
}

} // namespace erhe::graphics
