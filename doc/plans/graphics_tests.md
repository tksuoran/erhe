# Graphics tests: outstanding work

Status: proposed

Extends `doc/erhe/graphics_test_coverage.md` and
`doc/erhe/graphics_test_nonheadless_port.md`. The golden-image machinery and
the tests ported from the agfx suite are planned separately in
[`graphics_tests_agfx_port.md`](graphics_tests_agfx_port.md).

## GPU tests in CI under a software Vulkan

The job is in `.github/workflows/build.yml` and described in
`doc/testing.md` "CI". Locally, with the latest SDK tag's validation layer
built from source (the container could not reach the LunarG hosts), all 243
`gpu` tests pass on lavapipe with one capability skip. Outstanding: the
job's first run on a GitHub runner, which is also the first run of its
LunarG SDK download step.
