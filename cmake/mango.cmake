# Mango has CMakeLists.txt in build subdirectory

function (FetchContent_MakeAvailable_mango)
    FetchContent_GetProperties(mango)
    string(TOLOWER "mango" lc_mango)
    if (NOT ${lc_mango}_POPULATED)
        FetchContent_Populate(mango)

        # Disable archive formats that are not currently needed
        set(MANGO_ALL_ARCHIVE_FORMATS ZIP; RAR; MGX)
        foreach (format ${MANGO_ALL_ARCHIVE_FORMATS})
            option(MANGO_DISABLE_ARCHIVE_${format} "" ON)
        endforeach ()

        # Disable image fomrats that are not currently needed
        set(MANGO_ALL_IMAGE_FORMATS ASTC; ATARI; BMP; C64; DDS; GIF; HDR; IFF; JPG; KTX; PCX; PKM; PNM; PVR; SGI; TGA; WEBP; ZPNG)
        foreach (format ${MANGO_ALL_IMAGE_FORMATS})
            option(MANGO_DISABLE_IMAGE_${format} "" ON)
        endforeach ()

        add_subdirectory(${${lc_mango}_SOURCE_DIR}/build ${${lc_mango}_BINARY_DIR})
    endif ()
endfunction ()
