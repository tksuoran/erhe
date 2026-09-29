§MBEL:5.0
©erhe::Topic::graphics_tests
@scope::erhe_graphics_gpu_tests golden assertions (FLIP), agfx test port, GPU test verification loop
@docs::doc/erhe/graphics_test_coverage.md+doc/plans/graphics_tests_agfx_port.md+doc/plans/graphics_tests.md+doc/reference/agfx_comparison.md

[STATE]
@suite::Vk-headless-176-pass{build_vs2026_vulkan_headless}|GL-170-pass+12-skip(snorm+11-ray-query)+6-FAIL(AMD-GL-driver-comparison-sampler){build_tests}|Metal-not-run
@goldens::src/erhe/graphics/test/golden/{png+pfm+bin};update::ERHE_GPU_TEST_UPDATE_GOLDENS=1;report::py -3 scripts/gpu_test_report.py gpu_test_results
@FLIP::NVlabs/flip-single-header-via-CPM-archive-URL(no-tags)->erhe_gpu_test_support-only
@remaining::doc/plans/graphics_tests_agfx_port.md{52-blocked-tests-by-feature;open-findings}

[TRAPS]
!Image_loader-default=premultiplied→goldens-read-with-Alpha_mode::straight
!one-golden-set-all-backends::image-space-coords+texture_origin+native_depth_range;GL-bottom-left-origin-exercises-the-normalization
!AMD-GL-driver::comparison-sampler-reads-never-after-a-never-draw-of-identical-source{state-probed-correct;not-erhe}
!Vk-blit-dst-transition-from-UNDEFINED::latent-discard-risk{needs-per-subresource-layout-tracking;open}
!agfx_comparison.md-premise-stale::set_storage_image-works-on-Vk+GL+Metal(image_2d-level-0-only)

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
