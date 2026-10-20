§MBEL:5.0
©erhe::Topic::metal
@scope::Metal backend on macOS: threading with the dedicated render thread, autorelease pools, frame retirement, GPU fault diagnosis, macOS test status
@docs::doc/erhe/metal_backend.md+doc/plans/metal_backend.md+doc/editor/render_thread.md

[STATE_2026-10-19]
@editor-runs-on-Metal::windowed+render-thread+pipelining{before:crashed-first-frame}
@tests{build_xcode_metal,Debug,M4-Pro}::ctest-1753/1753{2026-10-19-after-d32fb2bb7;aa_cost_benchmark-passes}+editor-MCP-95/95{0-cb-errors}
@shadow-head-on-tests::pass-fresh+warm-editor{earlier-acne-reading=GPU-fault-artifact}

[LANDED_2026-10-19]
>b37c3050d::autorelease-pools-per-call+per-iteration{¬per-frame-slot-pool(d49944525-regression:acquire-on-main+end_frame-on-render=cross-thread-pop)}+cb/encoder/drawable-explicit-retain+thread_local-s_active_render_pass+s_active_encoder
>2842713c7::frame-retires-when-ALL-committed-cbs-completed{¬first-cb}+completion-handler-mutex+frame-id-mutex+is_frame_ended+capture-protocol-as-Vulkan{m_capture_mutex;frame-id-of-carrying-cb}+cb-error-logging
>051f28688::texture-heap-argument-buffer-re-encoded-every-frame-that-binds{ring-range-valid-only-its-frame;stale-range->garbage-texture->page-fault}
>be00d4361::tests:c_imgui_shortcut_modifier{super-on-Apple}+SIGPIPE-ignored-in-test-client

[LANDED_2026-10-19b]
>d32fb2bb7::Gpu_timer-on-Metal{stage-boundary-only-sampling(M4:draw/dispatch/blit-unsupported);encoder-start/end-via-sampleBufferAttachments;Gpu_timer_sample_pool{4096-sample-cap-per-buffer;256-sample-chunk-per-cb};resolve-in-cb-completion-handler;Ddgi-blend/relocate-own-encoders;doc/erhe/metal_backend.md"GPU-timers"}

[LANDED_2026-10-19c]
>9d8e64b28::thread-safety-audit{table->doc/erhe/metal_backend.md"Thread-safety";fixed:wait_idle-retired-frames-ended-during-wait(bound-now-read-before)+end_frame-slot-idle-under-frame_id_mutex+headless-capture-reads-newest-ENDED-composited-frame+role-asserts;open:headless-pacing->plan-item-7}
>ddc5a5923::gltf-primitive-dedup-slot-race{key-fields-written-once-under-mutex;payload-separate}
>AppleClang-ERHE_USE_TSAN{doc/agents/macos.md"ThreadSanitizer"};gates:image-4-configs-identical+TSan-0-reports

[VERIFIED_2026-10-19]
@ring-buffer-pool-on-Metal::Bistro-load-spills(3x5.6MB-main+render-4MB)+idle-reclaim-back-to-warm(main-1)+render-steady-plateau-~11x4MiB(8-buffer-warning-fires-but-no-growth-after-load){Vulkan-comparison-not-run-here}->plan-item-1-removed

[LANDED_2026-10-19d]
>7aac22819::acquire_frame_slot-waits-frame-F-N-GPU{headless-Bistro-pool-138->15-buffers}
>6da8cf158::resize:SDL-Metal-view-keeps-drawableSize;dead-resize_swapchain_to_window-removed(Metal+GL+null)
>1dd1f1f57::sort_depth_stencil_formats{capability-filter+Vulkan-order}
>034514447::Record_indexing::first_instance+use_base_instance{MSL-instance_id-includes-base;gpu-tests-187;Bistro-image-identical}
>a6cc14cc0::plan-items-5+6-need-decision{6:ICB-must-be-encoded-before-render-encoder-but-count-draw-is-in-pass}

[LANDED_2026-10-20]
>tier-S-on-Metal{init_frame_pacing:window-period-fed+Display_refresh_resolver::skip_calibration(was-calibrating-forever->actuators-idle);acquire/fence/present-records;layer-displaySync+3-drawables}
!2-drawables-halve-windowed-cadence{120Hz->60Hz-servo-idle;compositor-holds-displayed-drawable}->3
@tier-S-3-drawables::loop-8.58ms+sleep-7.2ms-at-clamp+nextDrawable-wait-5.6ms-left{queue-depth;needs-display-feedback}
>research::item-5-native-pacing-APIs+item-6-Metal-4{no-count-draw;executeCommandsInBuffer:indirectBuffer:-GPU-range+MSL-ICB-encode-kernel}->doc/plans/metal_backend.md
©User-2026-10-20:item-5=tier-S-built+research;item-6=skipped-for-now+research

[TRAPS]
!AutoreleasePool::per-thread-stack{drain-on-opening-thread;reverse-order}→¬span-device-frame
!retained-cb-still-in-pool::queue-slot(64)-freed-only-on-dealloc→create-in-local-pool+retain
!process-global-encoder-trackers::main-M2+render-thread-record-concurrently→thread_local
!argument-buffer-resources¬retained-by-cb→stale/freed-entries=page-fault¬validation-error
!ring-buffer-range::valid-only-allocating-frame{closed-but-unreleased-range-gets-reused}
!GPU-fault→after-2-faults-Metal-ignores-ALL-later-submissions{kIOGPUCommandBufferCallbackErrorSubmissionsIgnored}→rendering-silently-dead{screenshots-timeout;render_scene_image-empty}
!MTL_DEBUG_LAYER+MTL_SHADER_VALIDATION::hid-timing-fault{0/10-vs-10/15}
!ImGui-macOS::ConfigMacOSXBehaviors-swaps-Cmd/Ctrl{ImGuiMod_Ctrl-shortcut=Cmd};erhe-command-bindings-see-physical-Ctrl

!CounterSampleBuffer::max-32KiB{4096-timestamps}+empty-encoder-samples-read-0
!timers-inside-one-compute-encoder::all-read-whole-encoder→one-encoder-per-timed-pass

!TSan-macOS::symbolizes-itself->race:-suppressions-match;pending-report->SIGABRT-at-exit(__tsan::finalize)¬editor-crash;windowed-capture_screenshot-exceeds-MCP-request-timeout-under-TSan(still-processed)
!Metal-completion-blocks::TSan-false-positive(ordering-inside-Metal)->suppressed

[DIAGNOSIS_RECIPE]
cb-error::log_context-"Metal command buffer '<label>' of frame N failed"{permanent}
faulting-encoder::temporary-MTL::CommandBufferDescriptor+setErrorOptions(EncoderExecutionStatus)+log-error->description(){per-encoder-Faulted/Affected/Completed}
repro-loop::fresh-editor-per-run{ctest-R-3-tests;sleep-3-between;--repeat-breaks-fixtures}

[OPEN]→prompt_queue.txt+doc/plans/metal_backend.md{3-with-Vulkan(this-Mac-runs-MoltenVK+KosmicKrisp-too);4-interactive;5-tier-M+6-option-A/B-user-decision}+render-thread-gate-cases-not-run-on-Metal{bake,wide-line,skinned,four-view}
