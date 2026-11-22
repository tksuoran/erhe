§MBEL:5.0
©erhe::Topic::quest
@scope::Quest-3+Android-editor state: open items, landed fixes, traps, measurement rule
@docs::doc/agents/quest.md+doc/plans/quest_performance.md+doc/reference/quest_profiling_2026_05_01.md+doc/plans/xr.md+doc/plans/android.md+doc/plans/culling.md(Open)+doc/editor/render_thread.md(section-9-future-work)
@local::memory-bank/local/quest.md{device-numbers;gitignored}

[OPEN_2026-10-24]{priority-order;work-order=doc/plans/open_items.md}
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
@flat-draw-color-2026-10-31::Bistro-street-PVS-off-0-lights-ERHE_FLAT_DRAW_COLOR{occlusion-on-58.6fps-multiview-15.9ms-340-drawn|off-21.4fps-45.3ms-3395-drawn;vs-1-dir-light-49.6|20.4}->headset-frame=geometry-bound(vertex+binning)¬fragment-shading;position-pass-variant(no-lit-varyings)-same-57.6/21.4fps->varyings-not-the-cost;doc/plans/pvs.md-Verification
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
!culling-GPU-path-needs-drawIndirectFirstInstance+VK_KHR_draw_indirect_count->check-startup-log-"Record indexing"+classifier-capability-line;culling_classification-defaults-gpu(CPU-fallback-without-capability)
!measurement-rule::one-editor-at-a-time;nothing-building-on-host-during-timed-device-run
!build=scripts\build_android.bat quest assembleQuestRelease;install=scripts\install_android.bat quest release{uninstall-first}

[FOCUS_ARCHIVE_2026-11-07]{compacted-pointers;moved-from-activeContext.md}
@quest-headset-ux::DONE-2026-11-01{doc/agents/quest.md}
@quest-bistro-sweep::IN-PROGRESS-2026-10-29{doc/plans/quest_performance.md;doc/reference/quest_bench_runs.md}
@quest-performance::IN-PROGRESS-2026-10-24{doc/plans/quest_performance.md;memory-bank/topics/quest.md}

[PROGRESS_2026-11-07]{compacted-pointers;moved-from-progress.md}
[TASK::quest-performance]::archived{doc/plans/quest_performance.md;memory-bank/local/quest.md;open=?NEXT-heavy-line-compute-39ms+default-color-pass(grid-2ms;shadow-math);detail=git-log}

[FOCUS_ARCHIVE_2026-11-22]{moved-verbatim-from-activeContext.md;hashes+UNPUSHED-notes-as-written-then:everything-through-3b760af35-is-pushed}
@world-space-pieces::MEASURED-2026-11-08{735bfac9e;Quest-street-90Hz:whole-33.6fps|8m-25.7|5m-21.2->pieces-slower-on-Adreno;settle-fail-fixed(003316476-keying-scene_name);benchmark-views-head-locked(4ca6ec37b;©User-verified;runs-before-21:18-invalid);NEXT=why-more-draws-cost-more-on-Adreno;state=progress.md[TASK::world-space-pieces]}
@bistro-quest-benchmark::IN-PROGRESS-2026-11-08{bench=config/editor/benchmark_bistro_street_90.json(camera_path.type=animation:"Animation"/node"Camera";90Hz,720-frames)+benchmark_bistro_street.json(72Hz);configs=scripts/quest_bench_configs/bistro_headlamp_gpu_occlusion(_opaque_only).json;device-has-APK-17:11(animation-path-driver)+prepared-asset-with-camera-animation(/sdcard/.../files/bistro/bistro_src.quest.erhe_prepared,md5-3c3aa5b1);results:headlamp-36.6fps(eye-22.4ms-median),opaque-only-43.6fps(18.4ms);heaviest-second=t2-3s(bistro-corner,overdraw-8.3-rasterized/2.7-shaded-per-pixel,ALU-30%,vertex-fetch-stall-45%);NEXT=split-large-meshes(prepare-step)|path-edit;run-cmd=doc/agents/quest.md+progress.md}
@quest-fragment-shader::IN-PROGRESS-2026-11-08{plan=doc/plans/quest_fragment_shader.md;DONE=M1/M2-normal_offset-shadow-bias(aaab0b1e2;Adreno-740-637->367-instr,17->12-regs,100%-occupancy;desktop-gates-G1/G3/G4/G5-pass)+M5-identity-texture-transform(4be47d3c5)+OpenXR-preset-shadow_bias=normal_offset;step-0:fp16-paths-31-slower-on-device->M3-reduced-to-fp16-mask-1-in-OpenXR-preset-after-bench;device-bench-DONE(Bistro-street-58.7->108fps;OpenXR-preset-shader_fp16_paths=1);M4-DONE(23a32cc1d);NEXT=worn-visual-check-of-normal_offset-shadows+M6-BRDF-only-if-ALU-bound}

[PROGRESS_ARCHIVE_2026-11-22]{moved-verbatim-from-progress.md}
[TASK::quest-fragment-shader]{started-2026-11-08;branch=main;doc/plans/quest_fragment_shader.md}
✓plan{faf8033cc;shadow-lookup=60%-of-headset-lit-shader}
✓M1/M2-normal_offset{aaab0b1e2;offsets-1.0/1.0;shadow_verify-core-16bit:G1-0-acne,G3<=1.25t,G4-pass,G5<=1t(spot-2.5);G2=hardware-filter-softening}
✓M5-identity-texture-transform+OpenXR-preset{4be47d3c5}
✓device-bench{2026-11-08-12:04..12:08;Bistro-street-58.7->108fps,app-GPU-11.6->6.35ms;default-app-GPU-5.45->4.69ms;fp16-mask-1-level-on-Bistro,-8%-multiview-default->OpenXR-preset-shader_fp16_paths=1}
✓M4-16bit-material-record{23a32cc1d;material_record_half=auto(on-Android);320->192B;Adreno-13->12-regs,100%-occupancy;device-level(Bistro-105.8fps)}
[TASK::world-space-pieces]{started-2026-11-08;branch=main;commit-735bfac9e;doc/erhe/primitive.md"Pieces";scripts(scratchpad,not-committed):pieces_bench.py/pieces_lists.py/quest_export.py/quest_pieces.sh}
✓split+lod-border-locks+gltf-member+buffer-mesh-pieces+entries-per-piece+setting+key+unit-test+docs
✓desktop-measured{corner-pose,world-space-form:tris-267k->71k(8m)/65k(5m);raster-6.75->6.16/5.89;shaded-2.4-level;0-mismatches;screenshots-differ<0.1%}
✓settle-failure-root-cause{003316476;animation_create_key-resolved-"Camera"-in-empty-startup-"Scene 1"(--no-scene)->exports-lacked-Animation+camera;tools-take-scene_name+ambiguous-name=error;assets-re-exported+pushed}
✓benchmark-views-head-locked{4ca6ec37b;eye-views-latched-once(view-space)+projection-layer-in-VIEW-space;©User-verified-worn:path-followed+image-fixed;!all-Quest-bench-runs-before-2026-11-08-21:18-INVALID(©User)}
✓quest-measured{2026-11-08-21:22..21:27;one-input-hash;0m=33.6fps(multiview-25.0ms,457-entries)|8m=25.7fps(31.3ms,1388)|5m=21.2fps(37.4ms,2294);pieces-SLOWER-on-Adreno}
?next{find-why(per-draw-cost|binning-of-more-smaller-draws|split-vertices)-before-splitting-further}
[TASK::bistro-quest-benchmark]{started-2026-11-08;branch=main;doc/reference/quest_bench_runs.md(17:12..17:37-entries)+doc/plans/open_items.md"Quest session follow-ups"}
✓run-cmd{python3 scripts/quest_bench.py run --bench config/editor/benchmark_bistro_street_90.json --bench-to-device --editor-args=--no-scene --config scripts/quest_bench_configs/bistro_headlamp_gpu_occlusion.json --mcp 'set_graphics_preset={"shadow_bias":"normal_offset","shader_fp16_paths":1}' --load-scene /sdcard/Android/data/org.libsdl.app.quest/files/bistro/bistro_src.quest.erhe_prepared --mcp-measure get_lod_stats --record;off-head;add---perfetto|--gpu-metrics-for-counters(exclusive)}
✓findings{first-run-after-APK-install-11fps=one-off(repeat-33-34fps);LOD-threshold-64-active(levels-0-5-in-use);eye-pass-fragment-bound-by-overdraw-at-bistro-corner(t2-3s);alpha-tested+translucent=4ms/frame-of-22;opaque-alone-18.4ms(458k-tris,613-entries)}
!traps{MCP-set_node_transform-arg=translation(¬position:silently-"Nothing to set");device-migrated-config-stale-after-install(fp16-preset-via---mcp);desktop-editor-has-no-headlamp(headset-only)->desktop-renders-show-scene-lights;a-second-editor-takes-port-3744;disk-nearly-full(1.7GB-free;Debug-trees=next-cleanup)}
?next{opaque-draw-order-front-to-back/early-Z-check|shorten-or-re-pose-path-away-from-corner|Perfetto-render-stages-empty(surfaces-[])->investigate-metavr-capture}
