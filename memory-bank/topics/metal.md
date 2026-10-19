§MBEL:5.0
©erhe::Topic::metal
@scope::Metal backend on macOS: threading with the dedicated render thread, autorelease pools, frame retirement, GPU fault diagnosis, macOS test status
@docs::doc/erhe/metal_backend.md+doc/plans/metal_backend.md+doc/editor/render_thread.md

[STATE_2026-10-19]
@editor-runs-on-Metal::windowed+render-thread+pipelining{before:crashed-first-frame}
@tests{build_xcode_metal,Debug,M4-Pro}::ctest-1753/1753{2026-10-19-after-926fe35f3;aa_cost_benchmark-passes}+editor-MCP-95/95{0-cb-errors}
@shadow-head-on-tests::pass-fresh+warm-editor{earlier-acne-reading=GPU-fault-artifact}

[LANDED_2026-10-19]
>fabb99c76::autorelease-pools-per-call+per-iteration{¬per-frame-slot-pool(d49944525-regression:acquire-on-main+end_frame-on-render=cross-thread-pop)}+cb/encoder/drawable-explicit-retain+thread_local-s_active_render_pass+s_active_encoder
>01a871927::frame-retires-when-ALL-committed-cbs-completed{¬first-cb}+completion-handler-mutex+frame-id-mutex+is_frame_ended+capture-protocol-as-Vulkan{m_capture_mutex;frame-id-of-carrying-cb}+cb-error-logging
>294aecb56::texture-heap-argument-buffer-re-encoded-every-frame-that-binds{ring-range-valid-only-its-frame;stale-range->garbage-texture->page-fault}
>a45c59fe7::tests:c_imgui_shortcut_modifier{super-on-Apple}+SIGPIPE-ignored-in-test-client

[LANDED_2026-10-19b]
>926fe35f3::Gpu_timer-on-Metal{stage-boundary-only-sampling(M4:draw/dispatch/blit-unsupported);encoder-start/end-via-sampleBufferAttachments;Gpu_timer_sample_pool{4096-sample-cap-per-buffer;256-sample-chunk-per-cb};resolve-in-cb-completion-handler;Ddgi-blend/relocate-own-encoders;doc/erhe/metal_backend.md"GPU-timers"}

[LANDED_2026-10-19c]
>aab7b6e97::thread-safety-audit{table->doc/erhe/metal_backend.md"Thread-safety";fixed:wait_idle-retired-frames-ended-during-wait(bound-now-read-before)+end_frame-slot-idle-under-frame_id_mutex+headless-capture-reads-newest-ENDED-composited-frame+role-asserts;open:headless-pacing->plan-item-7}
>a03f87890::gltf-primitive-dedup-slot-race{key-fields-written-once-under-mutex;payload-separate}
>AppleClang-ERHE_USE_TSAN{doc/agents/macos.md"ThreadSanitizer"};gates:image-4-configs-identical+TSan-0-reports

[VERIFIED_2026-10-19]
@ring-buffer-pool-on-Metal::Bistro-load-spills(3x5.6MB-main+render-4MB)+idle-reclaim-back-to-warm(main-1)+render-steady-plateau-~11x4MiB(8-buffer-warning-fires-but-no-growth-after-load){Vulkan-comparison-not-run-here}->plan-item-1-removed

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

[OPEN]→prompt_queue.txt+doc/plans/metal_backend.md{items-3-7}+render-thread-gate-cases-not-run-on-Metal{bake,wide-line,skinned,four-view}
