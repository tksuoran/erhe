§MBEL:5.0
©erhe::Topic::graphics_tests
@scope::erhe_graphics_gpu_tests golden assertions (FLIP), agfx test port, GPU test verification loop
@docs::doc/erhe/graphics_test_coverage.md+doc/plans/graphics_tests_agfx_port.md+doc/plans/graphics_tests.md+doc/reference/agfx_comparison.md

[STATE]
@suite::Vk-headless-176-pass{build_vs2026_vulkan_headless}|Metal-175-pass+1-skip(draw_parameters_indirect:no-base-instance){build_xcode_metal}|macOS-Vk-165-pass+11-skip(ray-query-unsupported){build_xcode_vulkan}|GL-170-pass+12-skip(snorm+11-ray-query)+6-FAIL(AMD-GL-driver-comparison-sampler){build_tests}
@goldens::src/erhe/graphics/test/golden/{png+pfm+bin};update::ERHE_GPU_TEST_UPDATE_GOLDENS=1;report::py -3 scripts/gpu_test_report.py gpu_test_results
@FLIP::NVlabs/flip-single-header-via-CPM-archive-URL(no-tags)->erhe_gpu_test_support-only
@remaining::doc/plans/graphics_tests_agfx_port.md{52-blocked-tests-by-feature;open-findings}

[TRAPS]
!Gpu_test.device_up_clean-fails-with-distro-validation-layer-older-than-pinned-Vulkan-headers{VUID-VkDeviceCreateInfo-pNext-pNext-unknown-sType-1000558000=shader_relaxed_extended_instruction;use-SDK-layer;doc/agents/linux.md}
@scene_renderer-gpu-tests::30/30-since-f4a126842{Content_line_width-fixture-passed-temporary-Mesh_memory_config->dangling-ref;rvalue-ctor-now-deleted}
!Image_loader-default=premultiplied→goldens-read-with-Alpha_mode::straight
!one-golden-set-all-backends::image-space-coords+texture_origin+native_depth_range;GL-bottom-left-origin-exercises-the-normalization
!AMD-GL-driver::comparison-sampler-reads-never-after-a-never-draw-of-identical-source{state-probed-correct;not-erhe}
!Vk-blit-dst-transition-from-UNDEFINED::latent-discard-risk{needs-per-subresource-layout-tracking;open}
!agfx_comparison.md-premise-stale::set_storage_image-works-on-Vk+GL+Metal(image_2d-level-0-only)
!buffer-goldens-¬device-padding::Shader_resource::get_size_bytes(block)-pads-to-uniform_buffer_offset_alignment{Vk-iGPU-32|Metal-256}->golden-the-members-only{compute_atomics}

[PROGRESS]{done-2026-09-29;via-harness;one-opus-coder-per-phase}
✓plan::5ac16c1f9+8704ee38b(FLIP-from-start,user)
✓phase-0::6c5107fd9{golden-helpers+results.json+report.py}
✓phase-1::b395b5e53{25;Device_info::use_depth_clamp}
✓phase-2::b11d49cd1+d42ab3d05{18;Image_loader::Alpha_mode}
✓phase-3::2fb81325f+00c7d4fb9{9;Vk-EXTERNAL->0-dependency-READ-access;GL-tex->buffer-layer-count;GL-cube-is_layered}
✓phase-4::85ff0c263{10;buffer-goldens}
✓phase-5::7ac1c2996+c217a74b6{23;Vk-drawIndirectFirstInstance}
✓phase-6::b1bd453d7+392dff56e{10;GL-compute-set_sampled_image-no-op-fixed}
✓phase-7::4998557b3{6;GPU-written-indirect+command_barrier_bit}
✓phase-8::59462f723+1c3a87bc3{11;Vk-AS-build-input-16B-alignment}
✓metal-run::2026-09-30{175-pass+1-skip;2-fixes:compute_atomics-golden-members-only(540B,old-544B-minus-padding)+Results_listener-m_tests-brace-init->[[]]-json-305-no-results.json-ever-written;macOS-Vk-165-pass-cross-checks-golden;report.py-ok}
