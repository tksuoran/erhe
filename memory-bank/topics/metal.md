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
>item-4-resize::DONE-2026-10-20{user-interactive:resize+display-move+2x/1x/2x-mode-switch;all-pass}
>item-3-blit_framebuffer::DONE{one-contract-all-backends:clip_blit_framebuffer_region;Metal=copy|MSL-draw-between-formats;Vulkan=vkCmdBlitImage-offscreen-only(swapchain->doc/plans/xr.md);GL-end-coords-fixed(compile-unverified-no-GL-tree);Blit_framebuffer_test-3-pass-Metal+MoltenVK+KosmicKrisp}

[LANDED_2026-10-20b]
>item-5-tier-M-built{presented-handler->achieved_present_time+Present_feedback(shared_ptr;detach-at-teardown)+wait_for_displayed_frame;presentDrawableAtTime(target-T/4);GPUStart/EndTime->gpu_frame_begin/end;cpu_slot_end-in-end_frame(was-never-stamped);Device::is_present_target_time_honored}
>pacer::Deadline_offset_source{fixed|estimated}+delta_pe-estimator+FR5-wait_id-minus-floor(dpe/NT){claim-C22-C++-and-sim.py;77/77+27/27}
@probe-answers->doc/plans/metal_backend.md{clock-identical;targets-honored-windowed;presentedTime-on-grid-8.344ms;handler-0.48ms-off-main;presenting-cb-completes-at-GPU-end-16ms-before-display}
!compositor-deadline::windowed-needs-drawable~T+1.5ms-before-vsync{fixed-2.5ms->40%-2-refresh-frames}
@live-2026-10-20::M-estimated-at-N=1{1/348-two-refresh;release->display-19.8ms-vs-S-29.6ms;dpe~9.2ms}
!N=2-lock::startup-downshift->Apple-GPU-clocks-down->gpu-span-p95-7.6ms(11.6-no-pipelining)>upshift-bound-7.5ms->never-upshifts{pacer-outer-loop;needs-user-decision}
@auto=S{frame_pacing_tier-"w"=M};open->doc/plans/frame_pacing_metal.md
>2026-10-20b::C10-root-cause=in-flight-anchor-slot-collision-limit-cycle(deviation-16;fixed)+upshift-band-allowance(deviation-15)+C23;sim-81/81+C++-28/28
!live-GPU-cost-depends-on-cadence+start-phase{N=2-start-0.125-0.375->spans-9-14ms;N=1-5.5-6ms;Instruments-induces-perf-state->trace-hides-DVFS}->sim-plant-lacks-it
@tool::scripts/frame_pacing_live_gate.py{fresh-launches+span-by-phase}
!occluded-window->presentedTime-0->no-feedback{logged;blocked-live-run-2026-10-20}

[LANDED_2026-10-21]
>ec31329c4::present-calibration-at-device-init{tier-M;clear-only-probes-as-steady-stream-1-per-2-vsyncs;service_main_run_loop-needed(window-not-composited-before-first-CA-commit);ProMotion:request-1.0-1.75T+deadline-2.1T;fixed-60:0-0.75T+1.07T;MCP-get_frame_pacing_status.present_calibration-raw-probes}
>5b7a6d8ae::C24{predictive-downshift-uses-current-cadence-samples(N=2-GPU-tail-sent-pacer-back);FIFO-anchor(Present_queue_kind::fifo)}
>a08c0c038::Frame_time_recorder::now=mach_absolute_time-on-Apple+auto->M
@gate::ProMotion-Debug+Release-3-threading-modes-60/60-N=1-28ms(S-37.6)+fixed-60Hz-N=1-41ms(S-58.5)

[LANDED_2026-10-21b]
>ed9cb7a0e::thermal-state{NSProcessInfo;logged+MCP+gate}+Dell-calibration{request-0.38-0.5T;deadline-9.2ms}
>cb0414237+87ab403cf::Clock_monitor{IOReport-dlopen;GPUPH+PCPU-channels;pmgr-voltage-states9(GPU,Hz)+5/13-sram(P-clusters,kHz);sampler-thread-per-frame;gpu/cpu_clock_ratio}+C25{upshift-judged-by-work-at-clock-recorded-at-N-1}+join-wait-steady_clock->recorder-mapping-fix
>24610a4b6::Gpu_prewarm{ALU-kernel-after-presenting-cb-ordered-by-MTLEvent;rate-at-init}+C26{prewarm-phase-before-upshift;fill-learned-online-start-2ms-double-if-clock<0.9}{NOT-LIVE-GATED}
@measured::Dell-N=1-GPU-1470-1540MHz+P-cores-92-97%;N=2-GPU-~0.5(dips-0.41)+P-cores-~53%;work(time*clock)-cadence-invariant{GPU~5.0ms;CPU-Debug~1.1ms}
@measured::prewarm-probe{2.3ms-after-N=2-frame:clock-0.51->0.97,span-9.9->4.9ms}
@user-decisions-2026-10-21::IOReport-private-API-ok-for-clocks+prewarm-over-ramp-tolerant-attribution(judder-visible)+fill-learned-online

[LANDED_2026-10-21c]
>Dell-gate-PASSES{Release+Debug-x3-threading-60/60-N=1;two-refresh-0.17-0.34%;Release-17.9ms+Debug-19.9ms-vs-tier-S-31.5-34}
>fixes::calibration-majority-of-passes-per-request-offset+deadline-probe-spacing-floor(margin/T)+1+prewarm-rate-measured-first+prewarm-enabled-by-rate-alone(editor+observer)+C27-steady-clock-records{clock_settle_time-0.15s;n_used==N;¬prewarm-frames}
>tooling::live-gate--history(startup-transitions)+get_frame_pacing_frames.prewarm_ms
>C28-upshift-transient-FIXED{root=latency-trap¬CPU-clock:N=1-window-held-startup-samples+waits(clamp/join/acquire)-counted-as-latency->release-immediately-85-120-frames;fix=Frame_time_record::release_wait_time-excluded+per-visit-latency-windows-with-seed-floor;0-skips-after-54-upshifts;excess-0.3-1.1ms;no-QoS/core-request(©User-prefers-none)}
@open::60Hz+ProMotion-gate-with-this-build->doc/plans/frame_pacing_metal.md-Log

[LANDED_2026-10-23]
>0c5ff5285::GPU-read-draw-count{plan-sec-6-option-A;erhe::graphics-Indirect_count_draws+Compute_command_encoder::prepare_indexed_indirect_count_draws+Render_command_encoder::multi_draw_indexed_primitives_indirect_count(set,job);Metal=raw-MSL-kernel(2D-slot-x-job-grid;ICB-inheritPipelineState+inheritBuffers;range-buffer;per-frame-slot;1024-command-growth)+executeCommandsInBuffer;Vk/GL=native-count-draw;use_draw_indirect_count-true-on-Metal;Base_render_pipeline_create_info::indirect_count_draws-opt-in(Metal-refuses-supportIndirectCommandBuffers-for-fragment-shaders-with-directly-bound-textures/samplers);Draw_list_scene-shadow-GPU-commands-via-set-on-all-backends;gates:0-mismatch+cpu==gpu-images-default/heavy/Bistro-Metal+default/heavy-KK;Metal-gpu-tests-193/193}
!ICB-cost::~692B/command{creation-ok-to-4194304;driver-crash-8388608;1048576-range-executes}+setVertexBytes-inherited(ERHE_DRAW_ID-one-value-per-execute=0;records-by-first_instance)
!Bistro-ICB::66560-commands~46MB/frame-slot(784-jobs-x-chunk-max;832-drawn)->92033e2c9-per-shadow-pass-encode{prepare_gpu_shadow_pass-before-each-pass;set=slot+sequence;ICB-sized-to-largest-pass:Bistro-5120-commands-3.6MB/slot;table-buffer-ranges-linear-per-frame(grow-mid-frame-keeps-old-buffer-until-frame-completes)}
@metal-4-decisions-2026-10-23::count-first-then-MTL4;separate-backend-beside-Metal-3(own-ERHE_GRAPHICS_LIBRARY-value+dir);subsystem-by-subsystem-ctest-gated(order-in-plan-sec-7)

[TRAPS]
!compositor-shows-stretches-of-probes-a-refresh-late-at-any-offset(whole-passes)->judge-by-majority¬unanimity;request-at-target(offset-0)=window-edge
!probes-committed-before-predecessor-target->late-again(¬deadline-evidence)
!actual_vsyncs(MCP/window)=display-delta-to-NEXT-frame{forward-looking}
!pacer-latency-input-must-exclude-queue-waits{else-early-release-self-justifies}
!GPU-governor¬utilization-controller{N=2-68%-busy-at-0.45-clock;2.3ms-ALU-after-frame-raises-to-0.97}+ignores-compute-only-load-with-clear-frames(init-sweep-useless)
!prewarm-kernel-unordered-overlaps-frame(stretches-span)->MTLEvent-wait-after-presenting-cb
!any-steady_clock->recorder-time-conversion-must-map-via-now-in-both-clocks
!steady_clock!=CACurrentMediaTime-after-system-sleep{mach_absolute_time-stops-in-sleep}->recorder-uses-mach_absolute_time
!ProMotion-compositor=one-period-deeper{request-cutoff-V-T+deadline-2T+1ms}->calibrate¬hardcode
!blocked-nextDrawable-returns-at-latch-of-frame-k-2(display-15.7ms)
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

!MoltenVK::VK_GOOGLE_display_timing=presentDrawable:atTime:+presentedTime{mach_absolute_time-ns=recorder-clock;honored}->tier-W-via-display-timing;KosmicKrisp-has-no-timing-ext->tier-S
!Instruments-Metal-System-Trace-fixes-GPU-perf-state->MoltenVK-pass-read-2x-Metal(false);cost-model-from-untraced-records+IOReport-only
!Vulkan-frame-bracket-span-held-upload->frame-submit-idle-gap(1.4ms-Debug)->governor-lowers-clock-at-N=1(0.72)->span~T->spurious-downshift;fixed:gap-queries+gpu_service_time
!zsh-unquoted-$VAR-not-word-split{--env-$E-passes-one-arg}

!Bistro-2026-10-21::Apple-compiler(Metal+MoltenVK)-ran-unrolled-light-bodies-beyond-range->c8dc0080c-range-early-out{GPU-Metal-123->40ms;MoltenVK-108->35ms;KK-18->13ms;images-identical};remaining-~3x-GPU-gap-vs-KosmicKrisp-unexplained(no-lights-pass-also-slower)->open;KK-FIFO-not-vsync-locked(present-interval-13.7ms-stdev-3.8)
!SPIRV-Cross-precise->[[clang::optnone]]-spvFAdd/spvFSub/spvFMul-calls{Apple-compiler-cannot-inline;Bistro-Viewport-Metal-26.6ms-vs-KK-5.9}->fork-tksuoran/SPIRV-Cross-msl-inline-precise-helpers{MSL>=3.2-inline+#pragma-metal-fp-math_mode(safe)+contract(off)}+Metal-backend-MSL-3.2->5.5ms;MoltenVK-still-optnone-until-upstream;doc/plans/apple_gpu_shader_gap.md
!KK-MSL-dump::MESA_KK_DEBUG=msl+MESA_LOG=file+MESA_LOG_FILE=<path>{¬stderr-on-macOS}+move-KK-pipeline_cache-file-aside(else-deserialized-no-dump)
!saturated-GPU-A/B::frame_pacing_tier=off+force_disable_vsync=true->Viewport-timer-run-to-run-stable(26.6+-0.1ms);unsaturated-runs-drift-25-40ms
!bistro_frame_bench-on-macOS::tracy-capture-must-connect-after-editor-ready(c14396142);ERHE_TRACY_DIR=dir-with-symlinks-to-~/git/tracy/{capture,csvexport}/build/*

[DIAGNOSIS_RECIPE]
cb-error::log_context-"Metal command buffer '<label>' of frame N failed"{permanent}
faulting-encoder::temporary-MTL::CommandBufferDescriptor+setErrorOptions(EncoderExecutionStatus)+log-error->description(){per-encoder-Faulted/Affected/Completed}
repro-loop::fresh-editor-per-run{ctest-R-3-tests;sleep-3-between;--repeat-breaks-fixtures}

[LANDED_2026-10-23b]
>5d27306ab::metal4-scaffold{ERHE_GRAPHICS_API=metal4;defines-ERHE_GRAPHICS_API_METAL(family)+_METAL4;src/erhe/graphics/erhe_graphics/metal4/metal4_*(copy;same-pimpl-class-names;one-backend-per-tree);build_xcode_metal4;METAL_BACKEND=metal4-scripts/run_graphics_tests_metal.sh;generate.py-writes-both-format-tables;goldens-shared}
!MTL4-inseparable-core::CommandQueue::commit(MTL4::CommandBuffer-only)+CommandBuffer-yields-MTL4-encoders-only+encoders-bind-via-ArgumentTable-only(no-setBytes;blits-on-compute-encoder)->port-order=submission-core|counter-heaps|MTL4Compiler|ICB-check

[LANDED_2026-10-23c]
>434d9ecda::metal4-MTL4-submission-core{doc/erhe/metal4_backend.md;Recording-pool+CommitFeedback;Residency(one-set;removals-held-until-next-commit);ArgumentTable-per-encoder;draw-id-table-for-push-constant-15;inline-bytes-chunks;barrierAfterQueueStages(all)-every-encoder;present=queue-wait/signalDrawable/presentAtTime;MTL4CounterHeap}
!MTL4-retains-nothing::teardown-must-wait_idle-before-releasing-surface/pipelines;late-commits-must-be-charged-to-an-unretired-frame(else-deferred-release-frees-in-use-resources)
!MTL4-CommitOptions-reuse-across-commits->feedback-never-fires(wait_idle-hangs)->one-per-commit
!MTL4-barriers::stage-scoped-not-resource-scoped;barrierAfterEncoderStages=intra-pass-only;between-encoders-only-barrierAfterQueueStages-or-fences->explicit-usage-pairs-collapse-to-one-narrowed-queue-barrier-per-encoder-start
!©User-2026-10-23::no-backend-hazard-tracking(M2+R-record-concurrently);application-chains-usage_before/after;unconditional-encoder-start-barrier=band-aid-to-remove
!Vulkan-parity-baseline::attachment-usage_before/after(pre-pass-transition+post-pass-barrier)+memory_barrier(mask;src=ALL_COMMANDS)+transition_texture_layout/cmd_texture_barrier+blit-encoder-internal-transitions=what-every-backend-must-honour;Metal-4-maps-them-to-pending-stage-barriers(92938afb5-API-adds-narrowing-only)
!MTL4-queue-wait(event)-holds-ALL-later-commits{pre-warm-now-gates-next-frame;XR-producer-must-commit-before-consumer}

!MTL4CounterHeap-timestamps=ticks-of-queryTimestampFrequency(24MHz-mach_absolute_time)¬ns{sampleTimestamps+CommitFeedback-GPU-times-are-ns}->3c2bafd67-converts
!metal4-vs-metal3-unpaced-pipelined::2.91-vs-1.66ms{swapchain-term-2.2-vs-1.2ms;Tracy-zone-counts-uneven}->investigating-2026-10-23

!MTL4-queue->wait(drawable)-holds-the-whole-commit+all-later-commits->take-drawable-at-first-use(prefix-commit-before-swapchain-pass;24faccd5f);gap-between-the-two-commits=gpu_idle_time(else-pacer-reads-presentation-wait-as-GPU-load)
!MTL4-drawable-wait-tail::1-in-12-nextDrawable-6-8ms-with-displaySync-off(Metal-3:11/1211)->open
!GPU-tests::pacing-tier-must-be-off(calibration-2s-Metal/6s-MoltenVK-per-process;967d47edf)
!ring-buffer-range::cancel()-on-a-closed-WRITTEN-range-makes-no-sync-entry->space-stays-allocated;release()-after-close(8e0aaf42b-texture-heap+imgui)
!GPU-test-isolation::Gpu_test-fixture-snapshots-Device::get_debug_state-and-fails-on-growth(leaks-show-as-bytes_in_use)
!present-calibration-cache::key-from-OS-APIs-shared-by-metal/metal4/moltenvk(only-present_path-differs);window-mode-in-key;fullscreen-without-entry=pacer-defaults(old-windowed-1.38T-offset-in-fullscreen-was-wrong);ERHE_PRESENT_CALIBRATION_CACHE=0-for-gate-scripts;cache-dir-gitignored(cache/*)

[OPEN]→prompt_queue.txt+doc/plans/metal_backend.md{7-Metal-4:test-run-restructure->present-calibration-cache->hazard-tracking->MTL4Compiler;pacing-live-gate-pending-user}+render-thread-gate-cases-not-run-on-Metal{bake,wide-line,skinned,four-view}
