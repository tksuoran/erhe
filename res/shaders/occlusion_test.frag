// The raster occlusion test's fragments (occlusion_test.vert): the depth
// test runs before the fragment shader (early fragment tests: depth test on,
// depth write off, no color writes), so a fragment that runs is a visible
// fragment of the entry's box, and one marks the entry visible in the view:
// its bit in the view's group word of occlusion_result.words (the
// classifier's mask layout, uvec2 per (group, entry), as uint words).

layout(early_fragment_tests) in;

layout(location = 0) flat in uint v_word;
layout(location = 1) flat in uint v_mask;

void main()
{
    // Many fragments of one box set the same bit, and the passes of the
    // other views of the word's group set theirs: a GPU atomic OR is the
    // idiom for that (AGENTS.md's rule on lock-free techniques is about CPU
    // code). The plain read first skips the atomic once the bit is seen set:
    // the atomics of one large visible box on one word serialize, and they
    // were most of the pass's cost (doc/plans/occlusion_culling.md "Build
    // order"). A stale read only costs the atomic it would have skipped.
    if ((occlusion_result.words[v_word] & v_mask) == 0u) {
        atomicOr(occlusion_result.words[v_word], v_mask);
    }
}
