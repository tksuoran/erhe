#include "erhe_graphics/image_loader.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <mango/mango.hpp>
#include <cstdint>
#include <vector>

#define WUFFS_IMPLEMENTATION
//#define WUFFS_CONFIG__STATIC_FUNCTIONS

// https://github.com/google/wuffs/issues/151
// https://developercommunity.visualstudio.com/t/fatal--error-C1001:-Internal-compiler-er/10703305
#define WUFFS_CONFIG__AVOID_CPU_ARCH

#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
//#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
//#define WUFFS_CONFIG__MODULE__GIF
#define WUFFS_CONFIG__MODULE__JPEG
//#define WUFFS_CONFIG__MODULE__NETPBM
#define WUFFS_CONFIG__MODULE__NIE
#define WUFFS_CONFIG__MODULE__PNG
//#define WUFFS_CONFIG__MODULE__TGA
//#define WUFFS_CONFIG__MODULE__VP8
//#define WUFFS_CONFIG__MODULE__WBMP
//#define WUFFS_CONFIG__MODULE__WEBP
#define WUFFS_CONFIG__MODULE__ZLIB

//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ENABLE_ALLOWLIST
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGR_565
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGR
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGRA_NONPREMUL
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGRA_NONPREMUL_4X16LE
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGRA_PREMUL
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGB
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGBA_NONPREMUL
//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGBA_PREMUL

//#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_BGRA_PREMUL

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable : 4100) // unused
#   pragma warning(disable : 4127) // constant expression
#   pragma warning(disable : 4505) // unreferenced
#endif

// https://github.com/google/wuffs/issues/152
#if defined(__clang__)
#   pragma clang attribute push (__attribute__((no_sanitize("undefined"))), apply_to=function)
#endif

#include "wuffs-v0.4.c"

#if defined(__clang__)
#   pragma clang attribute pop
#endif
#ifdef _MSC_VER
#   pragma warning(pop)
#endif

const char* c_str(wuffs_base__pixel_format pixel_format)
{
    switch (pixel_format.repr) {
        case WUFFS_BASE__PIXEL_FORMAT__INVALID                : return "INVALID";
        case WUFFS_BASE__PIXEL_FORMAT__A                      : return "A";
        case WUFFS_BASE__PIXEL_FORMAT__Y                      : return "Y";
        case WUFFS_BASE__PIXEL_FORMAT__Y_16LE                 : return "Y_16LE";
        case WUFFS_BASE__PIXEL_FORMAT__Y_16BE                 : return "Y_16BE";
        case WUFFS_BASE__PIXEL_FORMAT__YA_NONPREMUL           : return "YA_NONPREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__YA_PREMUL              : return "YA_PREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__YCBCR                  : return "YCBCR";
        case WUFFS_BASE__PIXEL_FORMAT__YCBCRA_NONPREMUL       : return "YCBCRA_NONPREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__YCBCRK                 : return "YCBCRK";
        case WUFFS_BASE__PIXEL_FORMAT__YCOCG                  : return "YCOCG";
        case WUFFS_BASE__PIXEL_FORMAT__YCOCGA_NONPREMUL       : return "YCOCGA_NONPREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__YCOCGK                 : return "YCOCGK";
        case WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_NONPREMUL: return "INDEXED__BGRA_NONPREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_PREMUL   : return "INDEXED__BGRA_PREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_BINARY   : return "INDEXED__BGRA_BINARY";
        case WUFFS_BASE__PIXEL_FORMAT__BGR_565                : return "BGR_565";
        case WUFFS_BASE__PIXEL_FORMAT__BGR                    : return "BGR";
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL         : return "BGRA_NONPREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL_4X16LE  : return "BGRA_NONPREMUL_4X16LE";
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL            : return "BGRA_PREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL_4X16LE     : return "BGRA_PREMUL_4X16LE";
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_BINARY            : return "BGRA_BINARY";
        case WUFFS_BASE__PIXEL_FORMAT__BGRX                   : return "BGRX";
        case WUFFS_BASE__PIXEL_FORMAT__RGB                    : return "RGB";
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL         : return "RGBA_NONPREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL_4X16LE  : return "RGBA_NONPREMUL_4X16LE";
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL            : return "RGBA_PREMUL";
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL_4X16LE     : return "RGBA_PREMUL_4X16LE";
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_BINARY            : return "RGBA_BINARY";
        case WUFFS_BASE__PIXEL_FORMAT__RGBX                   : return "RGBX";
        case WUFFS_BASE__PIXEL_FORMAT__CMY                    : return "CMY";
        case WUFFS_BASE__PIXEL_FORMAT__CMYK                   : return "CMYK";
        default: return "?";
    }
}

namespace erhe::graphics {

class Image_loader_impl
{
public:
    Image_loader_impl ();
    ~Image_loader_impl() noexcept;
    Image_loader_impl (const Image_loader_impl&) = delete;
    auto operator=    (const Image_loader_impl&) = delete;
    Image_loader_impl (Image_loader_impl&&)      = delete;
    auto operator=    (Image_loader_impl&&)      = delete;

    [[nodiscard]] auto open(const std::filesystem::path& path, Image_info& image_info) -> bool;
    [[nodiscard]] auto open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info) -> bool;

    [[nodiscard]] auto load(std::span<std::uint8_t> transfer_buffer) -> bool;

    void close();

private:
    std::unique_ptr<mango::filesystem::File> m_file;
    Image_info                               m_info;
    wuffs_base__io_buffer                    m_io_buffer;
    wuffs_base__io_buffer_meta               m_io_buffer_meta;
    wuffs_base__image_decoder*               m_image_decoder{nullptr};
    union {
        //wuffs_bmp__decoder bmp;
        //wuffs_gif__decoder gif;
#if defined(WUFFS_CONFIG__MODULE__JPEG)
        wuffs_jpeg__decoder jpeg;
#endif
        //wuffs_netpbm__decoder netpbm;
#if defined(WUFFS_CONFIG__MODULE__NIE)
        wuffs_nie__decoder nie;
#endif
#if defined(WUFFS_CONFIG__MODULE__PNG)
        wuffs_png__decoder png;
#endif
        //wuffs_tga__decoder tga;
        //wuffs_wbmp__decoder wbmp;
        //wuffs_webp__decoder webp;
    } m_decoder_union;
    wuffs_base__image_config  m_image_config;
    wuffs_base__frame_config  m_frame_config;
    wuffs_base__pixel_buffer  m_pixel_buffer;
    std::vector<std::uint8_t> m_work_buffer_storage;
    wuffs_base__table_u8      m_pixel_buffer_table;
};

Image_loader_impl::Image_loader_impl()
{
}

Image_loader_impl::~Image_loader_impl() noexcept
{
    close();
}

auto Image_loader_impl::open(const std::filesystem::path& path, Image_info& info) -> bool
{
    ERHE_PROFILE_FUNCTION();

    close();

    m_file = std::make_unique<mango::filesystem::File>(path.string());
    mango::filesystem::File& file = *m_file;

    const std::span<const std::uint8_t>& buffer_view{const_cast<uint8_t*>(file.data()), file.size()};
    return open(buffer_view, info);
}

auto Image_loader_impl::open(const std::span<const std::uint8_t>& buffer_view, Image_info& info) -> bool
{
    ERHE_PROFILE_FUNCTION();

    wuffs_base__slice_u8 file_data{
        .ptr = const_cast<uint8_t*>(buffer_view.data()),
        .len = buffer_view.size()
    };

    m_io_buffer_meta = wuffs_base__make_io_buffer_meta(file_data.len, 0, 0, true);
    m_io_buffer = wuffs_base__make_io_buffer(file_data, m_io_buffer_meta);

    int32_t fourcc = wuffs_base__magic_number_guess_fourcc(file_data, true);

    switch (fourcc) {
#if defined(WUFFS_CONFIG__MODULE__JPEG)
        case WUFFS_BASE__FOURCC__JPEG: {
            wuffs_base__status initialize_status = wuffs_jpeg__decoder__initialize(
                &m_decoder_union.jpeg,
                sizeof m_decoder_union.jpeg,
                WUFFS_VERSION, 
                WUFFS_INITIALIZE__DEFAULT_OPTIONS
            );
            ERHE_VERIFY(wuffs_base__status__is_ok(&initialize_status));
            m_image_decoder = wuffs_jpeg__decoder__upcast_as__wuffs_base__image_decoder(&m_decoder_union.jpeg);
            break;
        }
#endif
#if defined(WUFFS_CONFIG__MODULE__PNG)
        case WUFFS_BASE__FOURCC__PNG: {
            wuffs_base__status initialize_status = wuffs_png__decoder__initialize(
                &m_decoder_union.png,
                sizeof m_decoder_union.png,
                WUFFS_VERSION, 
                WUFFS_INITIALIZE__DEFAULT_OPTIONS
            );
            ERHE_VERIFY(wuffs_base__status__is_ok(&initialize_status));
            m_image_decoder = wuffs_png__decoder__upcast_as__wuffs_base__image_decoder(&m_decoder_union.png);
            break;
        }
#endif
#if defined(WUFFS_CONFIG__MODULE__NIE)
        case WUFFS_BASE__FOURCC__NIE: {
            wuffs_base__status initialize_status = wuffs_nie__decoder__initialize(
                &m_decoder_union.nie,
                sizeof m_decoder_union.nie,
                WUFFS_VERSION, 
                WUFFS_INITIALIZE__DEFAULT_OPTIONS
            );
            ERHE_VERIFY(wuffs_base__status__is_ok(&initialize_status));
            m_image_decoder = wuffs_nie__decoder__upcast_as__wuffs_base__image_decoder(&m_decoder_union.nie);
            break;
        }
#endif
        default: {
            return false;
        }
    }

    ERHE_VERIFY(m_image_decoder != nullptr); // Paranoid. This should? never happen
    wuffs_base__status config_status = wuffs_base__image_decoder__decode_image_config(
        m_image_decoder,
        &m_image_config,
        &m_io_buffer
    );
    ERHE_VERIFY(wuffs_base__status__is_ok(&config_status));

    // Read the dimensions.
    constexpr uint32_t max_dimension = 65535;
    uint32_t width = wuffs_base__pixel_config__width(&m_image_config.pixcfg);
    uint32_t height = wuffs_base__pixel_config__height(&m_image_config.pixcfg);
    ERHE_VERIFY(width <= max_dimension);
    ERHE_VERIFY(height <= max_dimension);

    wuffs_base__pixel_format pixel_format = wuffs_base__pixel_config__pixel_format(&m_image_config.pixcfg);
    const char* format_name = c_str(pixel_format);
    static_cast<void>(format_name);

    switch (pixel_format.repr) {
        case WUFFS_BASE__PIXEL_FORMAT__INVALID                : break;
        case WUFFS_BASE__PIXEL_FORMAT__A                      : break;
        case WUFFS_BASE__PIXEL_FORMAT__Y                      : break;
        case WUFFS_BASE__PIXEL_FORMAT__Y_16LE                 : break;
        case WUFFS_BASE__PIXEL_FORMAT__Y_16BE                 : break;
        case WUFFS_BASE__PIXEL_FORMAT__YA_NONPREMUL           : break;
        case WUFFS_BASE__PIXEL_FORMAT__YA_PREMUL              : break;
        case WUFFS_BASE__PIXEL_FORMAT__YCBCR                  : break;
        case WUFFS_BASE__PIXEL_FORMAT__YCBCRA_NONPREMUL       : break;
        case WUFFS_BASE__PIXEL_FORMAT__YCBCRK                 : break;
        case WUFFS_BASE__PIXEL_FORMAT__YCOCG                  : break;
        case WUFFS_BASE__PIXEL_FORMAT__YCOCGA_NONPREMUL       : break;
        case WUFFS_BASE__PIXEL_FORMAT__YCOCGK                 : break;
        case WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_NONPREMUL: break;
        case WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_PREMUL   : break;
        case WUFFS_BASE__PIXEL_FORMAT__INDEXED__BGRA_BINARY   : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGR_565                : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGR                    : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL         : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL_4X16LE  : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL            : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL_4X16LE     : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGRA_BINARY            : break;
        case WUFFS_BASE__PIXEL_FORMAT__BGRX                   : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGB                    : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL         : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL_4X16LE  : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL            : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL_4X16LE     : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGBA_BINARY            : break;
        case WUFFS_BASE__PIXEL_FORMAT__RGBX                   : break;
        case WUFFS_BASE__PIXEL_FORMAT__CMY                    : break;
        case WUFFS_BASE__PIXEL_FORMAT__CMYK                   : break;
        default: break;
    }

    m_info.width       = static_cast<int>(width);
    m_info.height      = static_cast<int>(height);
    m_info.depth       = 1;
    m_info.level_count = 1;
    m_info.row_stride  = width * 4;
    m_info.format      = erhe::dataformat::Format::format_8_vec4_srgb;

    info = m_info;

    return true;
}

auto Image_loader_impl::load(std::span<std::uint8_t> destination) -> bool
{
    ERHE_PROFILE_FUNCTION();

    uint32_t width = wuffs_base__pixel_config__width(&m_image_config.pixcfg);
    uint32_t height = wuffs_base__pixel_config__height(&m_image_config.pixcfg);

    wuffs_base__pixel_config__set(&m_image_config.pixcfg, WUFFS_BASE__PIXEL_FORMAT__RGBA_PREMUL, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, width, height);

    //uint64_t pixel_count = static_cast<uint64_t>(m_info.width) * static_cast<uint64_t>(m_info.height);
    uint64_t pixel_buffer_length = wuffs_base__pixel_config__pixbuf_len(&m_image_config.pixcfg);
    if (destination.size() < pixel_buffer_length) {
        return false; // TODO
    }

    wuffs_base__status frame_config_status = wuffs_base__image_decoder__decode_frame_config(m_image_decoder, &m_frame_config, &m_io_buffer);
    if (!wuffs_base__status__is_ok(&frame_config_status)) {
        return false; // TODO
    }

    uint64_t workbuf_len = wuffs_base__image_decoder__workbuf_len(m_image_decoder).max_incl;

    //m_pixel_buffer_storage.resize(pixel_buffer_length);
    m_work_buffer_storage.resize(workbuf_len);

    wuffs_base__slice_u8 pixbuf_slice  = wuffs_base__make_slice_u8(destination.data(), destination.size());
    wuffs_base__slice_u8 workbuf_slice = wuffs_base__make_slice_u8(m_work_buffer_storage.data(), m_work_buffer_storage.size());

    wuffs_base__status pixel_buffer_status = wuffs_base__pixel_buffer__set_from_slice(&m_pixel_buffer, &m_image_config.pixcfg, pixbuf_slice);
    if (!wuffs_base__status__is_ok(&pixel_buffer_status)) {
        return false; // TODO
    }

    m_pixel_buffer_table = wuffs_base__pixel_buffer__plane(&m_pixel_buffer, 0);

    //while (true)
    {
        wuffs_base__status decode_frame_status = wuffs_base__image_decoder__decode_frame(
            m_image_decoder,
            &m_pixel_buffer,
            &m_io_buffer,
            WUFFS_BASE__PIXEL_BLEND__SRC,
            workbuf_slice,
            nullptr
        );
        if (!wuffs_base__status__is_ok(&decode_frame_status)) {
            return false;
        }
    }
    return true;
}

void Image_loader_impl::close()
{
    m_file.reset();
}

///////////////

Image_loader::Image_loader() 
    : m_impl{std::make_unique<Image_loader_impl>()}
{
}

Image_loader::~Image_loader() noexcept
{
    close();
}

auto Image_loader::open(const std::filesystem::path& path, Image_info& image_info) -> bool
{
    return m_impl->open(path, image_info);
}

auto Image_loader::open(const std::span<const std::uint8_t>& buffer_view, Image_info& image_info) -> bool
{
    return m_impl->open(buffer_view, image_info);
}

auto Image_loader::load(std::span<std::uint8_t> transfer_buffer) -> bool
{
    return m_impl->load(transfer_buffer);
}

void Image_loader::close()
{
    return m_impl->close();
}


} // namespace erhe::graphics

