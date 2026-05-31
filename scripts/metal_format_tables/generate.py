#!/usr/bin/env python3
"""
Author + emit the Metal pixel-format texture-capability table.

This script encodes the "Texture capabilities by pixel format" tables from
Apple's "Metal Feature Set Tables" PDF (Metal-Feature-Set-Tables.pdf, dated
"February 5, 2026" in-document) as an explicit per-format / per-family
capability matrix, then writes the standalone C++ table:

    src/erhe/graphics/erhe_graphics/metal/metal_pixel_format_table.cpp

The matrix below is hand-transcribed from the PDF text. Each capability cell is
a short string built from the legend letters:

    a Atomic   F Filter   W Write   C Color   B Blend
    M MSAA     R Resolve  S Sparse

"All" expands to F+W+C+B+M+R, and additionally S on families that support sparse
(Metal4 and Apple6..Apple10; per the PDF, Sparse is NOT part of "All" for Mac2,
Metal3 and Apple2..Apple5). `None` means "Not available" on that family.

Run:  py -3 scripts/metal_format_tables/generate.py   (or python3 ...)
"""

import os

# Column order matches Apple's tables and erhe::graphics::metal::Gpu_family.
FAMILIES = ['Metal3','Metal4','Apple2','Apple3','Apple4','Apple5',
            'Apple6','Apple7','Apple8','Apple9','Apple10','Mac2']
SPARSE = {1, 6, 7, 8, 9, 10}  # indices whose "All" includes Sparse

BIT = {'a':1,'F':2,'W':4,'C':8,'B':16,'M':32,'R':64,'S':128}

def A(i):   return 'FWCBMR' + ('S' if i in SPARSE else '')   # "All"
def INT(i): return 'WCM'    + ('S' if i in SPARSE else '')   # 8/16-bit integer

def row_all():          return [A(i)   for i in range(12)]
def row_int():          return [INT(i) for i in range(12)]

def by_family(metal3, metal4, apple2, apple3, apple4, apple5,
              apple6, apple7, apple8, apple9, apple10, mac2):
    return [metal3, metal4, apple2, apple3, apple4, apple5,
            apple6, apple7, apple8, apple9, apple10, mac2]

# A8Unorm: All everywhere except Apple2 = Filter only (page 9).
A8 = [A(i) if i != 2 else 'F' for i in range(12)]

# sRGB single/dual channel: Not available on Metal3 and Mac2; All otherwise.
def srgb_sd():
    return [None] + [A(i) for i in range(1, 11)] + [None]

# sRGB quad (RGBA8/BGRA8 _sRGB): FCMRB on Metal3 and Mac2; All otherwise.
def srgb_quad():
    return ['FCMRB'] + [A(i) for i in range(1, 11)] + ['FCMRB']

# 32-bit integer (R32Uint/Sint): Atomic on Metal3/Metal4/Apple6+/Mac2; no MSAA on
# Apple2..Apple6; Sparse on sparse families (page 10).
R32I = by_family('aWCM','aWCMS','WC','WC','WC','WC','aWCS',
                 'aWCMS','aWCMS','aWCMS','aWCMS','aWCM')
# 64-bit integer (RG32Uint/Sint): 64-bit atomics from Apple9 (footnote 12); no
# MSAA on Apple2..Apple6 (page 11).
RG32I = by_family('WCM','WCMS','WC','WC','WC','WC','WCS',
                  'WCMS','WCMS','aWCMS','aWCMS','WCM')
# 128-bit integer (RGBA32Uint/Sint): no MSAA on Apple families (128 bpp), no
# atomics; Sparse on sparse families (page 12).
RGBA32I = by_family('WCM','WCMS','WC','WC','WC','WC','WCS',
                    'WCMS','WCMS','WCMS','WCMS','WCM')

# 32/64/128-bit float: Write/Color/MSAA/Blend, no Filter/Resolve by default
# (Filter+Resolve gated by supports32BitFloatFiltering on Apple7/Apple8, applied
# at runtime). RG32Float / RGBA32Float lose MSAA on Apple2..Apple6 (pages 10-12).
R32F   = by_family('WCMB','WCMBS','WCMB','WCMB','WCMB','WCMB','WCMBS',
                   'WCMBS','WCMBS','WCMBS','WCMBS','WCMB')
RG32F  = by_family('WCMB','WCMBS','WCB','WCB','WCB','WCB','WCBS',
                   'WCMBS','WCMBS','WCMBS','WCMBS','WCMB')
RGBA32F = RG32F[:]  # RGBA32Float follows the same Apple "no-MSAA" pattern

# RGB10A2Uint: Apple2 lacks Write (page 11).
RGB10A2UI = by_family('WCM','WCMS','CM','WCM','WCM','WCM','WCMS',
                      'WCMS','WCMS','WCMS','WCMS','WCM')
# RGB10A2Unorm / RG11B10Float: All except Apple2 = FCMRB (packed; pages 11).
PACKED_ALL = by_family('FWCBMR', A(1), 'FCMRB', A(3),A(4),A(5),A(6),
                       A(7),A(8),A(9),A(10), 'FWCBMR')
# RGB9E5Float: Metal3 Filter, Metal4 All, Apple2 FCMRB, Apple3..10 All, Mac2 Filter.
RGB9E5 = by_family('F', A(1), 'FCMRB', A(3),A(4),A(5),A(6),
                   A(7),A(8),A(9),A(10), 'F')

# Packed 16-bit render formats (B5G6R5 etc.): Apple-GPU only, not writable, not
# on Intel Mac2 (page 10).
PACKED16 = by_family('FCMRB','FCMRBS','FCMRB','FCMRB','FCMRB','FCMRB',
                     'FCMRBS','FCMRBS','FCMRBS','FCMRBS','FCMRBS', None)

# Extended range / wide color (BGRA10_XR etc.): Metal4 + Apple3..10 (page 13).
XR = by_family(None, A(1), None, A(3),A(4),A(5),A(6),A(7),A(8),A(9),A(10), None)

# Depth / stencil (pages 12-13). Depth filtering of 32-bit float depth is gated
# by 32-bit float filtering (Apple7+); Depth16 is always filterable.
def depth16(i): return 'FMR' + ('S' if i in SPARSE else '')
DEPTH16 = [depth16(i) for i in range(12)]
DEPTH32 = by_family('MR','MRS','MR','MR','MR','MR','MRS',
                    'FMRS','FMRS','FMRS','FMRS','MR')
D32S8   = DEPTH32[:]                                   # Depth32Float_Stencil8
STENCIL8 = by_family('MR','MRS','M','M','M','MR','MRS',
                     'MRS','MRS','MRS','MRS','MR')
D24S8 = [None]*11 + ['FMR']                            # Mac2 only (+ runtime)
X24S8 = [None]*11 + ['MR']                             # Mac2 only
X32S8 = ['M']*12

# YUV (page 12): Filter only.
YUV = ['F']*12

# BC compressed (page 3 + page 12): Apple9+, Mac2; Filter sample-only, +Sparse
# on Apple9/Apple10.
BC = by_family(None,None,None,None,None,None,None,None,None,'FS','FS','F')

# ---------------------------------------------------------------------------
# The table: PDF format name -> 12-family capability strings.
# Format names map 1:1 to MTL::PixelFormat<Name>.
# ---------------------------------------------------------------------------
TABLE = {}

def add(names, vals):
    for n in names:
        TABLE[n] = vals

# Ordinary, fully "All" on every family (page 9-12).
add(['R8Unorm','R8Snorm','R16Float','RG8Unorm','RG8Snorm','RG16Float',
     'RGBA8Unorm','RGBA8Snorm','BGRA8Unorm','RGBA16Float','BGR10A2Unorm',
     # 16-bit Unorm/Snorm: All on Apple4+/Metal3/Metal4/Mac2; the Apple2/Apple3
     # tables drop only Resolve. Encoded as All (exact for modern hardware).
     'R16Unorm','R16Snorm','RG16Unorm','RG16Snorm','RGBA16Unorm','RGBA16Snorm'],
    row_all())

add(['A8Unorm'], A8)
add(['R8Unorm_sRGB','RG8Unorm_sRGB'], srgb_sd())
add(['RGBA8Unorm_sRGB','BGRA8Unorm_sRGB'], srgb_quad())

add(['R8Uint','R8Sint','R16Uint','R16Sint','RG8Uint','RG8Sint',
     'RG16Uint','RG16Sint','RGBA8Uint','RGBA8Sint','RGBA16Uint','RGBA16Sint'],
    row_int())

add(['R32Uint','R32Sint'], R32I)
add(['RG32Uint','RG32Sint'], RG32I)
add(['RGBA32Uint','RGBA32Sint'], RGBA32I)
add(['R32Float'], R32F)
add(['RG32Float'], RG32F)
add(['RGBA32Float'], RGBA32F)

add(['RGB10A2Uint'], RGB10A2UI)
add(['RGB10A2Unorm','RG11B10Float'], PACKED_ALL)
add(['RGB9E5Float'], RGB9E5)

add(['B5G6R5Unorm','A1BGR5Unorm','ABGR4Unorm','BGR5A1Unorm'], PACKED16)
add(['BGRA10_XR','BGRA10_XR_sRGB','BGR10_XR','BGR10_XR_sRGB'], XR)

add(['GBGR422','BGRG422'], YUV)

add(['Depth16Unorm'], DEPTH16)
add(['Depth32Float'], DEPTH32)
add(['Stencil8'], STENCIL8)
add(['Depth32Float_Stencil8'], D32S8)
add(['Depth24Unorm_Stencil8'], D24S8)
add(['X24_Stencil8'], X24S8)
add(['X32_Stencil8'], X32S8)

# BC compressed (desktop + Apple9+). ASTC/EAC/ETC/PVRTC are Apple-family,
# Filter-only formats; they are intentionally omitted here (not used by erhe on
# macOS) and can be appended following the same pattern.
add(['BC1_RGBA','BC1_RGBA_sRGB','BC2_RGBA','BC2_RGBA_sRGB','BC3_RGBA',
     'BC3_RGBA_sRGB','BC4_RUnorm','BC4_RSnorm','BC5_RGUnorm','BC5_RGSnorm',
     'BC6H_RGBFloat','BC6H_RGBUfloat','BC7_RGBAUnorm','BC7_RGBAUnorm_sRGB'], BC)

def mask(s):
    if s is None:
        return 0
    m = 0
    for ch in s:
        m |= BIT[ch]
    return m

def emit_cpp(path):
    rows = []
    for name in sorted(TABLE.keys()):
        vals = TABLE[name]
        assert len(vals) == 12, name
        masks = [mask(v) for v in vals]
        rows.append((name, masks))
    lines = []
    lines.append('// AUTO-GENERATED by scripts/metal_format_tables/generate.py')
    lines.append('// Source: Apple "Metal Feature Set Tables" (Metal-Feature-Set-Tables.pdf).')
    lines.append('// Do not edit by hand; edit the generator and re-run.')
    lines.append('')
    lines.append('#include "erhe_graphics/metal/metal_pixel_format_table.hpp"')
    lines.append('')
    lines.append('#include <Metal/Metal.hpp>')
    lines.append('')
    lines.append('#include <array>')
    lines.append('#include <cstdint>')
    lines.append('')
    lines.append('namespace erhe::graphics::metal {')
    lines.append('')
    lines.append('namespace {')
    lines.append('')
    lines.append('// One row per documented MTLPixelFormat. caps[Gpu_family] holds the OR of')
    lines.append('// Texture_capability bits supported by that family.')
    lines.append('struct Format_caps_row')
    lines.append('{')
    lines.append('    MTL::PixelFormat format;')
    lines.append('    const char*      name;')
    lines.append('    std::uint8_t     caps[12];')
    lines.append('};')
    lines.append('')
    lines.append(f'constexpr std::array<Format_caps_row, {len(rows)}> c_format_caps{{{{')
    for name, masks in rows:
        masks_txt = ', '.join('0x%02x' % m for m in masks)
        lines.append(f'    {{ MTL::PixelFormat{name}, "{name}", {{ {masks_txt} }} }},')
    lines.append('}};')
    lines.append('')
    lines.append('[[nodiscard]] auto find_row(const std::uint64_t mtl_pixel_format) -> const Format_caps_row*')
    lines.append('{')
    lines.append('    const MTL::PixelFormat format = static_cast<MTL::PixelFormat>(mtl_pixel_format);')
    lines.append('    for (const Format_caps_row& row : c_format_caps) {')
    lines.append('        if (row.format == format) {')
    lines.append('            return &row;')
    lines.append('        }')
    lines.append('    }')
    lines.append('    return nullptr;')
    lines.append('}')
    lines.append('')
    lines.append('} // anonymous namespace')
    lines.append('')
    lines.append('auto metal_pixel_format_texture_capabilities(')
    lines.append('    const std::uint64_t mtl_pixel_format,')
    lines.append('    const Gpu_family    family')
    lines.append(') noexcept -> std::uint32_t')
    lines.append('{')
    lines.append('    const Format_caps_row* const row = find_row(mtl_pixel_format);')
    lines.append('    if (row == nullptr) {')
    lines.append('        return texture_capability_none;')
    lines.append('    }')
    lines.append('    const int index = static_cast<int>(family);')
    lines.append('    if ((index < 0) || (index >= static_cast<int>(Gpu_family::count))) {')
    lines.append('        return texture_capability_none;')
    lines.append('    }')
    lines.append('    return static_cast<std::uint32_t>(row->caps[index]);')
    lines.append('}')
    lines.append('')
    lines.append('auto metal_pixel_format_is_known(const std::uint64_t mtl_pixel_format) noexcept -> bool')
    lines.append('{')
    lines.append('    return find_row(mtl_pixel_format) != nullptr;')
    lines.append('}')
    lines.append('')
    lines.append('auto metal_gpu_family_name(const Gpu_family family) noexcept -> const char*')
    lines.append('{')
    lines.append('    switch (family) {')
    for i, fam in enumerate(FAMILIES):
        enumname = fam.lower()
        lines.append(f'        case Gpu_family::{enumname}: return "{fam}";')
    lines.append('        default: return "?";')
    lines.append('    }')
    lines.append('}')
    lines.append('')
    lines.append('auto metal_resolve_gpu_family(MTL::Device* const device) noexcept -> Gpu_family')
    lines.append('{')
    lines.append('    if (device == nullptr) {')
    lines.append('        return Gpu_family::metal3;')
    lines.append('    }')
    lines.append('    // Prefer the most capable Apple family; its column is a superset of Mac2 on')
    lines.append('    // Apple silicon (which reports both). Apple9 covers Apple10 devices too.')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple9))  { return Gpu_family::apple9; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple8))  { return Gpu_family::apple8; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple7))  { return Gpu_family::apple7; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple6))  { return Gpu_family::apple6; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple5))  { return Gpu_family::apple5; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple4))  { return Gpu_family::apple4; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple3))  { return Gpu_family::apple3; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyApple2))  { return Gpu_family::apple2; }')
    lines.append('    if (device->supportsFamily(MTL::GPUFamilyMac2))    { return Gpu_family::mac2;   }')
    lines.append('    return Gpu_family::metal3;')
    lines.append('}')
    lines.append('')
    lines.append('} // namespace erhe::graphics::metal')
    lines.append('')
    with open(path, 'w') as f:
        f.write('\n'.join(lines))
    return len(rows)

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, '..', '..'))
    out = os.path.join(repo, 'src', 'erhe', 'graphics', 'erhe_graphics',
                       'metal', 'metal_pixel_format_table.cpp')
    n = emit_cpp(out)
    print(f'wrote {n} format rows to {out}')
