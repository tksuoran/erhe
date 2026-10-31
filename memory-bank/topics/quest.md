§MBEL:5.0
©erhe::Topic::quest
@scope::Quest-3+Android-editor state: open items, landed fixes, traps, measurement rule
@docs::doc/agents/quest.md+doc/plans/quest_performance.md+doc/reference/quest_profiling_2026_05_01.md+doc/plans/xr.md+doc/plans/android.md+doc/plans/culling.md(Open)+doc/editor/render_thread.md(section-9-future-work)
@local::memory-bank/local/quest.md{device-numbers;gitignored}

[OPEN_2026-10-24]{priority-order;mirrored-in-prompt_queue.txt}
1✓baseline-DONE-2026-10-24{benchmark_orbit@120Hz-repeat-3;both-scenes-GPU-bound;numbers->local/quest.md+plan-Baseline;render-thread-overlap-VERIFIED;was:phase-timing-log-M1/M2/R+ovrgpuprofiler;default-scene+heavier-scene;commit-id-in-log-first-line;expect:xrWaitFrame(N+1)-inside-R(N)+fps+XR_FB_performance_metrics+passthrough+quad-layers+controller-rays+ID-pick-intact;numbers->local/quest.md;method->doc/plans/quest_performance.md}
2!headset-passes-unculled{multiview-never-culled;per-eye-stopped-at-phase-6a;fix=erhe::xr-locate-eye-views-once-in-pre_execute_rendergraph_node+Xr_session::render_frame-reuses+union-frustum-view-for-multiview-color-pass;doc/plans/culling.md-Open}
3✓texture-sampling-DONE-2026-10-24{no-mip/sampler-defect;shadow-compare+compositor-fetches;preset-_version-bug-fixed;one-dir-shadow-light;open:glTF-default-sampler-nearest;grid-2ms;shadow-math-1ms/light;was:re-measure-first;mips-not-sampled(mip_level_count=1+min_lod=max_lod=0)=highest;nearest-vs-linear-audit;FFR-level;texture-compression;fp16-revisit-deferred}
4?headset-prepare-phase{headset-records-tools+renders-in-R-from-live-state->frame_pipelining-forced-off-under-OpenXR;move-to-M2-prepare+R-patch-views;hover-entries-read-in-R;xrEndFrame-submits-outside-device-queue-mutex}
5?small-warts{request_renderdoc_capture-MCP-tool-built-never-exercised;"ID Render color"->TRANSFER_SRC_OPTIMAL-with-WRITE-access-mask(should-be-READ);right-trigger-right-click-threshold>0-no-hysteresis;Hover_entry.mask=Raytrace_node_mask-bits-vs-Hover_entry::*_bit-consumers;is_head_attached_ray_mode()-0-callers;laser_begin-joint-could-replace-hand-authored-ray-origin}
?long-term{doc/plans/xr.md:XR_EXT_render_model-pair-when-runtime-ships+battery-quad-recheck-after-OS-update+ASTC/BC7-controller-textures+OpenXR_mirror-GL/Metal;doc/plans/android.md:phone-editor-phase-3}

[BENCHMARK_MODE]{©User-2026-10-24}
@status::LANDED-2026-10-24{2792e1422+139dbdc17+f0e9d344c;quest_bench.py-run/compare;120Hz-needs-setprop-debug.oculus.refreshRate(script-does-it);input_hash-identical-across-runs;spread-0.05-0.7%}
@requirement::Quake-timedemo{fake-clock-frame_dt/frame->same-frames-regardless-of-speed;result=wall_time_s+fps-first,breakdown-second;xrWaitFrame-pins-wall-time-at-refresh->record-frames_at_refresh_ceiling+optional-display_refresh_rate_hz(120)}+fully-deterministic+repeatable{frame-indexed-camera-path;fixed-frame_dt-simulation-time;measure-only-after-scene-settled;no-randomness;input_hash-in-result;--repeat-N-spread-visible}->then-record-NEW-baseline(default+heavy;replaces-2026-10-24-worn-headset-numbers-as-reference)
@spec::doc/plans/quest_performance.md-"Benchmark mode"

[LANDED]{all-Quest-verified;unpushed-since-2026-10-11-on-branch-render-thread}
>a186e22c2-2026-10-11::Adreno-740-refuses-variable-descriptor-count<half-layout-width->Device_impl-probes-at-init+get_texture_heap_variable_count_minimum();max_texture_heap_size-1024-Android
>375eba772::XR-single-thread-latch-removed{"XR GL thread error"-spam-from-render-thread}
>bd798c93f::NDK-r28-clang-19-needs--Wno-missing-designated-field-initializers-under--Werror
>2026-08-14::Vulkan-validation-end-to-end{layer-.so-bundled;knob=vulkan_validation_layers-in-config/editor/erhe_graphics.json;uninstall-wipes-SPIR-V-cache}+debug-label-scope-redesign{Scoped_debug_group-cb-level+Scoped_queue_debug_group}
>2026-08-13::XR_FB_render_model-controllers+control-bones+HUD/Hotbar-quad-layers(#209)+FFR(#208;Vulkan-only)+passthrough-init(#214)+context-menu-thumbrest-fix
>2026-05-02::multiview=Vulkan-only(©User)

[GPU_BREAKDOWN_2026-10-24]{491a843f0;per-pass-timers-in-benchmark-result}
default::XR-multiview-7.09ms(=app-frame;grid-2.0;shadow-math~1/light);compositor-1.75ms-passthrough-off(2.42-on);ImGui-hotbar-0.04
heavy::line-compute-39.3ms(!)+XR-multiview-21.3+shadow-1.41=61.7(app_gpu-52.7;timer-spans-include-compositor-preemption)
!next::edge/wide-line-compute-runs-for-every-mesh-in-headset-pass->make-it-cost-only-what-is-shown
?open::compositor-slowdown-still-occurs-2026-10-31(3/56-Bistro-street-runs;compositor-GPU>3ms-vs-1.6;multiview+30-65ms;passthrough-off;flag-and-rerun);compositor-time-bimodal-1.35|2.1ms-after-passthrough-off(unknown-cause);passthrough-pause-not-verified-compositor-side;glTF-default-sampler-nearest;worn-visual-check-draw-lists+one-shadow-light

[OFF_HEAD_RUNS]{VERIFIED-2026-10-24}
✓recipe::force-stop+KEYCODE_WAKEUP+am-broadcast-com.oculus.vrpowermanager.prox_close+setprop-debug.oculus.guardian_pause-1->normal-VR-launch->FOCUSED-in-4s;72Hz-full-tick-13.88ms==worn;GPU-43%;compositor-shows-layer;no-Controllers-Required-dialog;restore=force-stop+automation_disable+guardian_pause-0
@implication::benchmarks+soaks-need-no-wearer{headset-prompt-protocol-exception-proposed-to-user}

[TRAPS]
!headset-prompt-protocol::install-FIRST->ask-user-headset-on->launch-only-on-acknowledged-readiness;one-confirmation-per-launch(batch/soak=one-up-front)
!start-scripts/quest_logcat.sh-BEFORE-every-launch;never-blind-sleep->poll-readiness
!capture_screenshot-cannot-work-in-XR->RenderDoc-Meta-fork=only-pixel-level-path{scripts/quest_renderdoc.py}
!config-is-APK-bundled{config-edits->rebuild+uninstall+install}
!Quest-runtime-hides-extensions-without-manifest-gate{logcat-tag-OpenXR_Properties}
!culling-GPU-path-needs-drawIndirectFirstInstance+VK_KHR_draw_indirect_count->check-startup-log-"Record indexing"+classifier-capability-line;culling_classification-defaults-cpu
!measurement-rule::one-editor-at-a-time;nothing-building-on-host-during-timed-device-run
!build=scripts\build_android.bat quest assembleQuestRelease;install=scripts\install_android.bat quest release{uninstall-first}
