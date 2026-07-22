// VK_ENABLE_BETA_EXTENSIONS is set as a PRIVATE compile definition for the
// erhe_graphics target (see CMakeLists.txt) so the portability-subset structs
// are visible everywhere in this library, not just in this translation unit.
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_scoped_debug_group.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/vulkan_external_creators.hpp"

#include "erhe_utility/env.hpp"
#include "erhe_window/window.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace erhe::graphics {

[[nodiscard]] auto get_vendor(const uint32_t vendor_id) -> Vendor
{
    switch (vendor_id) {
        case 0x00001002u: return Vendor::Amd;
        default: return Vendor::Unknown;
    }
}

auto Device_impl_debug_report_callback(
    VkDebugReportFlagsEXT      flags,
    VkDebugReportObjectTypeEXT object_type,
    uint64_t                   object,
    size_t                     location,
    int32_t                    message_code,
    const char*                layer_prefix,
    const char*                message,
    void*                      user_data
) -> VkBool32;

[[nodiscard]] auto Device_impl_debug_utils_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void*                                       user_data
) -> VkBool32;

void set_env_or_warn(const char* key, const char* value)
{
    if (!erhe::utility::set_env(key, value)) {
        log_context->warn("Setting {}={} environment variable failed.", key, value);
    }
}

#if defined(ERHE_OS_MACOS)
// Parse a dotted version string like "1.4.341.0" into integer components.
// Returns an empty vector if any component is not a non-negative integer.
auto parse_version_components(const std::string& name) -> std::vector<int>
{
    std::vector<int> parts;
    std::stringstream ss(name);
    std::string token;
    while (std::getline(ss, token, '.')) {
        if (token.empty()) {
            return {};
        }
        for (char c : token) {
            if (c < '0' || c > '9') {
                return {};
            }
        }
        try {
            parts.push_back(std::stoi(token));
        } catch (...) {
            return {};
        }
    }
    return parts;
}

// Locate the macOS subdirectory of the highest-versioned LunarG Vulkan SDK install
// (e.g. $HOME/VulkanSDK/1.4.341.0/macOS). Used when VULKAN_SDK is not set in the
// environment -- which is normal for GUI-launched apps (Xcode/Finder/Dock) on macOS,
// since they do not inherit the shell's environment.
// Returns an empty string if no SDK is found.
auto find_vulkan_sdk_macos_dir() -> std::string
{
    const char* home = std::getenv("HOME");
    if ((home == nullptr) || (home[0] == '\0')) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::path(home) / "VulkanSDK";
    if (!std::filesystem::is_directory(root, ec)) {
        return {};
    }
    std::vector<std::pair<std::vector<int>, std::filesystem::path>> candidates;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::filesystem::path macos_dir = entry.path() / "macOS";
        if (!std::filesystem::is_directory(macos_dir, ec)) {
            continue;
        }
        std::vector<int> version = parse_version_components(entry.path().filename().string());
        if (version.empty()) {
            continue;
        }
        candidates.emplace_back(std::move(version), macos_dir);
    }
    if (candidates.empty()) {
        return {};
    }
    std::sort(
        candidates.begin(), candidates.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; }
    );
    return candidates.back().second.string();
}
#endif

// Collects validation layer settings, then materializes the VkLayerSettingEXT array in finalize().
// After finalize() the internal vectors are not modified, so the addresses handed to Vulkan via
// VkLayerSettingEXT::pValues remain valid for the lifetime of this object.
class Validation_layer_settings
{
public:
    static constexpr const char* c_layer_name = "VK_LAYER_KHRONOS_validation";

    void add_bool(const char* key, const bool value)
    {
        Entry entry{};
        entry.key        = key;
        entry.kind       = Kind::bool_value;
        entry.bool_value = value ? VK_TRUE : VK_FALSE;
        m_entries.push_back(std::move(entry));
    }

    void add_string(const char* key, std::string value)
    {
        Entry entry{};
        entry.key          = key;
        entry.kind         = Kind::string_value;
        entry.string_value = std::move(value);
        m_entries.push_back(std::move(entry));
    }

    void finalize()
    {
        const std::size_t entry_count = m_entries.size();
        m_string_c_strs.reserve(entry_count);
        m_settings.reserve(entry_count);
        for (Entry& entry : m_entries) {
            VkLayerSettingEXT setting{
                .pLayerName   = c_layer_name,
                .pSettingName = entry.key,
                .type         = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
                .valueCount   = 1,
                .pValues      = nullptr
            };
            switch (entry.kind) {
                case Kind::bool_value: {
                    setting.type    = VK_LAYER_SETTING_TYPE_BOOL32_EXT;
                    setting.pValues = &entry.bool_value;
                    break;
                }
                case Kind::string_value: {
                    m_string_c_strs.push_back(entry.string_value.c_str());
                    setting.type    = VK_LAYER_SETTING_TYPE_STRING_EXT;
                    setting.pValues = &m_string_c_strs.back();
                    break;
                }
            }
            m_settings.push_back(setting);
        }
    }

    [[nodiscard]] auto get_settings() const -> const std::vector<VkLayerSettingEXT>&
    {
        return m_settings;
    }

private:
    enum class Kind { bool_value, string_value };
    class Entry
    {
    public:
        const char* key          = nullptr;
        Kind        kind         = Kind::bool_value;
        VkBool32    bool_value   = VK_FALSE;
        std::string string_value;
    };

    std::vector<Entry>             m_entries;
    std::vector<const char*>       m_string_c_strs;
    std::vector<VkLayerSettingEXT> m_settings;
};

Device_impl::Device_impl(
    Device&                         device,
    const Surface_create_info&      surface_create_info,
    const Graphics_config&          graphics_config,
    const Vulkan_external_creators* vulkan_external_creators
)
    : m_context_window   {surface_create_info.context_window}
    , m_device           {device}
    , m_graphics_config  {graphics_config}
    , m_shader_monitor   {device}
    , m_external_creators{vulkan_external_creators}
{
    {
        // Disable nuisance implicit layers, but not when RenderDoc is attached
        // (RenderDoc uses an implicit layer for Vulkan capture)
#if defined(ERHE_OS_WINDOWS)
        const bool renderdoc_attached = (GetModuleHandleA("renderdoc.dll") != nullptr);
        if (!renderdoc_attached) {
            set_env_or_warn("VK_LOADER_LAYERS_DISABLE", "~implicit~");
        }
        set_env_or_warn("DISABLE_LAYER_NV_OPTIMUS_1", "1");
        set_env_or_warn("DISABLE_LAYER_NV_GR2608_1", "1");
        set_env_or_warn("DISABLE_VULKAN_OBS_CAPTURE", "1");
#endif
#if defined(ERHE_OS_MACOS)
        //set_env_or_warn("VK_LOADER_LAYERS_DISABLE", "~implicit~");
        if (graphics_config.vulkan.use_moltenvk) {
            // Force the MoltenVK ICD (Vulkan-on-Metal). Useful when KosmicKrisp is also
            // installed, since the loader would otherwise enumerate both and may pick
            // KosmicKrisp; pinning VK_DRIVER_FILES makes the choice explicit. Prefer the
            // ICD shipped with the Vulkan SDK, falling back to a system-wide install.
            // Must be set before volkInitialize() / vkCreateInstance().
            std::string vulkan_sdk_dir;
            const char* vulkan_sdk_env = std::getenv("VULKAN_SDK");
            if ((vulkan_sdk_env != nullptr) && (vulkan_sdk_env[0] != '\0')) {
                vulkan_sdk_dir = vulkan_sdk_env;
            } else {
                vulkan_sdk_dir = find_vulkan_sdk_macos_dir();
            }
            std::error_code ec;
            std::string     icd_path;
            if (!vulkan_sdk_dir.empty()) {
                const std::string sdk_icd = fmt::format("{}/share/vulkan/icd.d/MoltenVK_icd.json", vulkan_sdk_dir);
                if (std::filesystem::exists(sdk_icd, ec)) {
                    icd_path = sdk_icd;
                }
            }
            if (icd_path.empty()) {
                const std::string system_icd = "/usr/local/share/vulkan/icd.d/MoltenVK_icd.json";
                if (std::filesystem::exists(system_icd, ec)) {
                    icd_path = system_icd;
                }
            }
            if (icd_path.empty()) {
                log_context->warn(
                    "use_moltenvk is enabled but no MoltenVK_icd.json was found "
                    "(checked the Vulkan SDK and /usr/local/share/vulkan/icd.d). Install the LunarG SDK or molten-vk."
                );
            } else {
                log_context->info("MoltenVK selected: VK_DRIVER_FILES={}", icd_path);
                set_env_or_warn("VK_DRIVER_FILES", icd_path.c_str());
            }
            if (graphics_config.vulkan.use_kosmickrisp) {
                log_context->warn("Both use_moltenvk and use_kosmickrisp are enabled; using MoltenVK (use_moltenvk takes precedence).");
            }
        }
        if (graphics_config.vulkan.use_kosmickrisp && !graphics_config.vulkan.use_moltenvk) {
            // KosmicKrisp ICD lives at <vulkan_sdk>/share/vulkan/icd.d/libkosmickrisp_icd.json
            // (LunarG SDK >= 1.4.335). Point the loader at it explicitly so it is used
            // instead of MoltenVK. Must be set before volkInitialize() / vkCreateInstance().
            //
            // VULKAN_SDK is set by the SDK's setup-env.sh and is typically present in a shell
            // that sourced it, but GUI launches on macOS (Xcode, Finder, Dock) do not inherit
            // shell environment, so fall back to scanning $HOME/VulkanSDK/*/macOS.
            std::string vulkan_sdk_dir;
            const char* vulkan_sdk_env = std::getenv("VULKAN_SDK");
            if ((vulkan_sdk_env != nullptr) && (vulkan_sdk_env[0] != '\0')) {
                vulkan_sdk_dir = vulkan_sdk_env;
                log_context->info("KosmicKrisp: using VULKAN_SDK from environment: {}", vulkan_sdk_dir);
            } else {
                vulkan_sdk_dir = find_vulkan_sdk_macos_dir();
                if (!vulkan_sdk_dir.empty()) {
                    log_context->info(
                        "KosmicKrisp: VULKAN_SDK not set in environment; using auto-detected SDK: {}",
                        vulkan_sdk_dir
                    );
                }
            }
            if (vulkan_sdk_dir.empty()) {
                log_context->warn(
                    "use_kosmickrisp is enabled but no Vulkan SDK found. "
                    "Install the LunarG SDK to ~/VulkanSDK/<version>/ or set VULKAN_SDK in the launch environment."
                );
            } else {
                const std::string icd_path = fmt::format("{}/share/vulkan/icd.d/libkosmickrisp_icd.json", vulkan_sdk_dir);
                std::error_code ec;
                if (!std::filesystem::exists(icd_path, ec)) {
                    log_context->warn(
                        "use_kosmickrisp is enabled but {} does not exist. "
                        "KosmicKrisp requires LunarG SDK 1.4.335 or newer.",
                        icd_path
                    );
                } else {
                    log_context->info("KosmicKrisp selected: VK_DRIVER_FILES={}", icd_path);
                    set_env_or_warn("VK_DRIVER_FILES", icd_path.c_str());
                }
            }
        }
#endif
    }

    VkResult result{VK_SUCCESS};

    result = volkInitialize();
    if (result != VK_SUCCESS) {
        log_context->critical("volkInitialize() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    uint32_t instance_layer_count{0};
    result = vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceLayerProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    // Setup vulkan instance layers
    std::vector<VkLayerProperties> instance_layers(instance_layer_count);
    result = vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceLayerProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    for (const VkLayerProperties& layer : instance_layers) {
        log_debug->info(
            "Vulkan Instance Layer: {} spec_version {:08x} implementation_version {:08x} {}",
            layer.layerName,
            layer.specVersion,
            layer.implementationVersion,
            layer.description
        );
    }

    std::vector<const char*> enabled_instance_layers_c_str;
    auto check_layer = [&](const char* name, bool& enable)
    {
        for (const VkLayerProperties& layer : instance_layers) {
            if (strcmp(layer.layerName, name) == 0) {
                enabled_instance_layers_c_str.push_back(name);
                enable = true;
                log_debug->info("  Enabling {}", layer.layerName);
                return;
            }
        }
    };

    if (
        graphics_config.vulkan.vulkan_validation_layers &&
        !graphics_config.renderdoc_capture_support
    ) {
        check_layer("VK_LAYER_KHRONOS_validation", m_instance_layers.m_VK_LAYER_KHRONOS_validation);
    }

    //check_layer("VK_LAYER_LUNARG_crash_diagnostic", m_instance_layers.m_VK_LAYER_LUNARG_crash_diagnostic);

    uint32_t instance_extension_count{0};
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    std::vector<VkExtensionProperties> instance_extensions(instance_extension_count);
    result = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, instance_extensions.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkEnumerateInstanceExtensionProperties() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    // Create vulkan instance
    const VkApplicationInfo application_info{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = nullptr,
        .pApplicationName   = "ERHE",
        .applicationVersion = 0,
        .pEngineName        = "ERHE",
        .engineVersion      = 2025012,
        .apiVersion         = VK_API_VERSION_1_3
    };

    // Setup vulkan instance extensions

    auto is_instance_extension_advertised = [&](const char* name) -> bool {
        for (const VkExtensionProperties& extension : instance_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    // SDL_Vulkan_GetInstanceExtensions() will request KHR_SURFACE and platform specific required instance extensions.
    // Filter SDL's list against what the loader actually advertises so we don't request an extension that would make
    // vkCreateInstance() fail (the RenderDoc capture layer hides some extensions from vkEnumerateInstanceExtensionProperties).
    // VK_KHR_portability_enumeration is handled separately below: on macOS it must be enabled for MoltenVK even when it
    // is not advertised through the (RenderDoc-filtered) list, so it is intentionally not subject to this filter.
    // Headless (no native window surface): run Vulkan surfaceless with an
    // emulated swapchain. Skip all surface / swapchain instance + device
    // extensions and the present queue.
    const bool headless = (m_context_window == nullptr) || !m_context_window->has_vulkan_surface();

    std::vector<const char*> enabled_instance_extensions_c_str{};
    bool portability_enumeration_enabled = false;
    if (m_context_window != nullptr) {
        const std::vector<std::string>& required_extensions = m_context_window->get_required_vulkan_instance_extensions();
        log_debug->info("SDL_Vulkan_GetInstanceExtensions() returned {} extension(s):", required_extensions.size());
        for (const std::string& extension_name : required_extensions) {
            log_debug->info("  {}", extension_name);
        }
        for (const std::string& extension_name : required_extensions) {
            if (!is_instance_extension_advertised(extension_name.c_str())) {
                log_debug->info("  Skipping SDL-requested extension {} (not advertised by loader)", extension_name);
                continue;
            }
            enabled_instance_extensions_c_str.push_back(extension_name.c_str());
            log_debug->info("  Enabling SDL-requested extension {}", extension_name);
            if (strcmp(extension_name.c_str(), VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
                portability_enumeration_enabled = true;
            }
        }
    }

#if defined(ERHE_OS_MACOS)
    // MoltenVK is a portability-subset driver: per the Vulkan spec the instance must enable
    // VK_KHR_portability_enumeration and set VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
    // otherwise the loader rejects MoltenVK with VK_ERROR_INCOMPATIBLE_DRIVER. The extension is
    // provided by the *loader* (whenever a portability ICD such as MoltenVK is installed), but
    // the RenderDoc capture layer does not re-advertise it through
    // vkEnumerateInstanceExtensionProperties, and current SDL does not include it in its
    // required-extension list either - so the advertised-list filter above never enables it.
    // Enable it explicitly here so MoltenVK works with or without the capture layer. On a
    // native macOS Vulkan driver (e.g. KosmicKrisp) this flag is harmless - it only widens
    // physical-device enumeration to also include portability drivers.
    if (!portability_enumeration_enabled) {
        enabled_instance_extensions_c_str.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        portability_enumeration_enabled = true;
        log_debug->info("  Enabling {} (macOS portability driver requirement)", VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
#endif

    auto check_instance_extension = [&](const char* name, bool& enable)
    {
        for (const VkExtensionProperties& extension : instance_extensions) {
            if (strcmp(extension.extensionName, name) == 0) {
                enabled_instance_extensions_c_str.push_back(name);
                enable = true;
                log_debug->info("  Enabling {}", extension.extensionName);
                return;
            }
        }
    };

    for (const VkExtensionProperties& extension : instance_extensions) {
        log_debug->info(
            "Vulkan Instance Extension: {} spec_version {:08x}",
            extension.extensionName,
            extension.specVersion
        );
    }

    // First check extensions that have not been deprecated / promoted to core.
    // Surface-capability / swapchain-colorspace extensions are surface-dependent
    // and must not be requested in headless (surfaceless) mode.
    if (!headless) {
        check_instance_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME, m_instance_extensions.m_VK_KHR_get_surface_capabilities2);
        check_instance_extension(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME     , m_instance_extensions.m_VK_KHR_surface_maintenance1     );
    }
    check_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME               , m_instance_extensions.m_VK_EXT_debug_utils              );
    if (!headless) {
        check_instance_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME     , m_instance_extensions.m_VK_EXT_swapchain_colorspace     );
    }

    // Check extensions which are promoted to core or deprecated
    if (application_info.apiVersion < VK_API_VERSION_1_1) {
        check_instance_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, m_instance_extensions.m_VK_KHR_get_physical_device_properties2);
    } else {
        // VK_KHR_get_surface_capabilities2 is surface-dependent (not core in 1.1);
        // do not assume it in headless mode.
        if (!headless) {
            m_instance_extensions.m_VK_KHR_get_surface_capabilities2 = true;
        }
        m_instance_extensions.m_VK_KHR_get_physical_device_properties2 = true;
    }

    m_capabilities.m_surface_capabilities2 = m_instance_extensions.m_VK_KHR_get_surface_capabilities2;

    if (!headless && !m_instance_extensions.m_VK_KHR_surface_maintenance1) {
        check_instance_extension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME, m_instance_extensions.m_VK_EXT_surface_maintenance1);
    }

    if (!m_instance_extensions.m_VK_EXT_debug_utils) {
        check_instance_extension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME, m_instance_extensions.m_VK_EXT_debug_report);
    }

    VkInstanceCreateInfo instance_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = portability_enumeration_enabled ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0u,
        .pApplicationInfo        = &application_info,
        .enabledLayerCount       = static_cast<uint32_t>(enabled_instance_layers_c_str.size()),
        .ppEnabledLayerNames     = enabled_instance_layers_c_str.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(enabled_instance_extensions_c_str.size()),
        .ppEnabledExtensionNames = enabled_instance_extensions_c_str.data()
    };

    // https://vulkan.lunarg.com/doc/sdk/1.4.328.1/windows/khronos_validation_layer.html
    // https://vulkan.lunarg.com/doc/view/1.4.328.1/windows/layer_configuration.html
    // https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/layers/vk_layer_settings.txt
    Validation_layer_settings                 validation_settings;
    std::vector<VkValidationFeatureEnableEXT> enabled_validation_features;
    //std::vector<VkValidationFeatureDisableEXT> disabled_validation_features;
    VkValidationFeaturesEXT validation_features{
        .sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext                          = nullptr,
        .enabledValidationFeatureCount  = 0,
        .pEnabledValidationFeatures     = nullptr,
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures    = nullptr,
    };
    VkLayerSettingsCreateInfoEXT layer_settings_create_info{
        .sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext        = nullptr,
        .settingCount = 0,
        .pSettings    = nullptr,
    };
    if (m_instance_layers.m_VK_LAYER_KHRONOS_validation) {
        instance_create_info.pNext = &validation_features;
        validation_features.pNext = &layer_settings_create_info;
        enabled_validation_features.push_back(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
        enabled_validation_features.push_back(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
        validation_features.enabledValidationFeatureCount = static_cast<uint32_t>(enabled_validation_features.size());
        validation_features.pEnabledValidationFeatures = enabled_validation_features.data();

        validation_settings.add_string("debug_action", "VK_DBG_LAYER_ACTION_CALLBACK");

        validation_settings.add_bool("validate_core",                     true);
        validation_settings.add_bool("check_image_layout",                true);
        validation_settings.add_bool("check_command_buffer",              true);
        validation_settings.add_bool("check_object_in_use",               true);
        validation_settings.add_bool("check_query",                       true);
        validation_settings.add_bool("check_shaders",                     true);
        validation_settings.add_bool("check_shaders_caching",             true);
        validation_settings.add_bool("unique_handles",                    true);
        validation_settings.add_bool("object_lifetime",                   true);
        validation_settings.add_bool("stateless_param",                   true);
        validation_settings.add_bool("thread_safety",                     true);
        validation_settings.add_bool("validate_sync",                     true);
        validation_settings.add_bool("syncval_submit_time_validation",    true);
        validation_settings.add_bool("syncval_shader_accesses_heuristic", true);
        validation_settings.add_bool("syncval_message_extra_properties",  true);

        // https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/11207
        validation_settings.add_bool("validate_best_practices",           true);
        //validation_settings.add_bool("validate_best_practices_amd",       true);
        //validation_settings.add_bool("validate_best_practices_arm",       true);
        //validation_settings.add_bool("validate_best_practices_img",       true);
        //validation_settings.add_bool("validate_best_practices_nvidia",    true);

        validation_settings.finalize();

        layer_settings_create_info.settingCount = static_cast<uint32_t>(validation_settings.get_settings().size());
        layer_settings_create_info.pSettings    = validation_settings.get_settings().data();
    }

    log_context->info(
        "vkCreateInstance() with flags = 0x{:08x}, {} layer(s), {} extension(s):",
        static_cast<uint32_t>(instance_create_info.flags),
        instance_create_info.enabledLayerCount,
        instance_create_info.enabledExtensionCount
    );
    for (uint32_t i = 0; i < instance_create_info.enabledLayerCount; ++i) {
        log_context->info("  layer:     {}", instance_create_info.ppEnabledLayerNames[i]);
    }
    for (uint32_t i = 0; i < instance_create_info.enabledExtensionCount; ++i) {
        log_context->info("  extension: {}", instance_create_info.ppEnabledExtensionNames[i]);
    }

    if ((m_external_creators != nullptr) && static_cast<bool>(m_external_creators->create_instance)) {
        log_context->info("Creating Vulkan instance via external (XR) hook");
        result = m_external_creators->create_instance(&instance_create_info, &m_vulkan_instance);
    } else {
        result = vkCreateInstance(&instance_create_info, nullptr, &m_vulkan_instance);
    }
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateInstance() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    volkLoadInstance(m_vulkan_instance);

    // Create vulkan surface
    std::unique_ptr<Surface_impl> surface_impl{};
    VkSurfaceKHR vulkan_surface{VK_NULL_HANDLE};
    if (m_context_window != nullptr) {
        surface_impl   = std::make_unique<Surface_impl>(*this, surface_create_info, headless);
        vulkan_surface = surface_impl->get_vulkan_surface();
    }

    std::vector<const char*> device_extensions_c_str{};
    const bool physical_device_ok = choose_physical_device(surface_impl.get(), device_extensions_c_str);
    if (!physical_device_ok) {
        // NOTE: this is choose_physical_device() failing, NOT vkCreateInstance()
        // (which already succeeded above). The most common cause for the windowed
        // build is that the display is powered off / asleep / disconnected: the
        // window swapchain then has no present-capable queue, every device scores
        // 0, and no physical device is selected. The headless build is immune
        // because it is surfaceless.
        if (!headless) {
            log_context->critical(
                "No suitable Vulkan physical device with a present-capable queue was found for the window "
                "surface. This usually means the display is powered off, asleep, or disconnected (a windowed "
                "swapchain needs a present queue on the surface). Switch to the HEADLESS build, which renders "
                "to an emulated swapchain and needs no display: "
                "scripts/configure_vs2026_vulkan_headless.bat -> "
                "build_vs2026_vulkan_headless/src/editor/Debug/editor.exe (ERHE_WINDOW_LIBRARY=none)."
            );
        } else {
            log_context->critical("choose_physical_device() failed: no suitable Vulkan physical device was found.");
        }
        abort();
    }

    if (headless) {
        // No present queue exists in headless; alias its family index to the
        // graphics family so the device-frame command pool (created on the
        // present family in ensure_device_frame_command_buffer) lands on the
        // graphics family.
        m_present_queue_family_index = m_graphics_queue_family_index;
    }

    m_memory_properties = VkPhysicalDeviceMemoryProperties2{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext            = nullptr,
        .memoryProperties = {
            .memoryTypeCount = 0,
            .memoryTypes     = {},
            .memoryHeapCount = 0,
            .memoryHeaps     = {}
        }
    };
    vkGetPhysicalDeviceMemoryProperties2(m_vulkan_physical_device, &m_memory_properties);

    m_depth_stencil_resolve_properties = VkPhysicalDeviceDepthStencilResolveProperties{
        .sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES,
        .pNext                       = nullptr,
        .supportedDepthResolveModes  = 0,
        .supportedStencilResolveModes= 0,
        .independentResolveNone      = VK_FALSE,
        .independentResolve          = VK_FALSE
    };
    m_driver_properties = VkPhysicalDeviceDriverProperties{
        .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        .pNext              = &m_depth_stencil_resolve_properties,
        .driverID           = VK_DRIVER_ID_MAX_ENUM,
        .driverName         = {},
        .driverInfo         = {},
        .conformanceVersion = {},
    };
    // When VK_KHR_portability_subset is supported we chain the properties
    // struct into the property2 query below. When it is NOT supported we
    // leave m_portability_subset_properties at "no constraints" defaults
    // (alignment 1) so call sites can read the value unconditionally.
    m_portability_subset_properties = VkPhysicalDevicePortabilitySubsetPropertiesKHR{
        .sType                                = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_PROPERTIES_KHR,
        .pNext                                = nullptr,
        .minVertexInputBindingStrideAlignment = 1u
    };
    if (m_device_extensions.m_VK_KHR_portability_subset) {
        m_portability_subset_properties.pNext = m_driver_properties.pNext;
        m_driver_properties.pNext             = &m_portability_subset_properties;
    }
    // VkPhysicalDeviceMultiviewProperties is core in Vulkan 1.1; chain
    // unconditionally on a 1.1+ device. Kept local: spliced into the local
    // physical_device_properties2.pNext, not into the m_driver_properties
    // member, so its address does not outlive this function scope.
    VkPhysicalDeviceMultiviewProperties query_multiview_properties{
        .sType                     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES,
        .pNext                     = &m_driver_properties,
        .maxMultiviewViewCount     = 0,
        .maxMultiviewInstanceIndex = 0,
    };
    VkPhysicalDeviceProperties2 physical_device_properties2 {
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext      = &query_multiview_properties,
        .properties = {}
    };
    vkGetPhysicalDeviceProperties2(m_vulkan_physical_device, &physical_device_properties2);
    VkPhysicalDeviceProperties& properties = physical_device_properties2.properties;
    const uint32_t api_version_variant = VK_API_VERSION_VARIANT(properties.apiVersion);
    const uint32_t api_version_major   = VK_API_VERSION_MAJOR  (properties.apiVersion);
    const uint32_t api_version_minor   = VK_API_VERSION_MINOR  (properties.apiVersion);
    const uint32_t api_version_patch   = VK_API_VERSION_PATCH  (properties.apiVersion);
    const VkConformanceVersion& conformance = m_driver_properties.conformanceVersion;
    log_context->info("Vulkan physical device properties:");
    log_context->info("  API version         = {}.{}.{}.{}", api_version_major, api_version_minor, api_version_patch, api_version_variant);
    log_context->info("  Driver ID           = {} ({})",     c_str(m_driver_properties.driverID), static_cast<uint32_t>(m_driver_properties.driverID));
    log_context->info("  Driver name         = {}",          m_driver_properties.driverName);
    log_context->info("  Driver info         = {}",          m_driver_properties.driverInfo);
    log_context->info("  Driver version      = {:08x}",      properties.driverVersion);
    log_context->info("  Driver conformance  = {}",          conformance.major, conformance.minor, conformance.subminor, conformance.patch);
    log_context->info("  Vendor ID           = {:08x}",      properties.vendorID);
    log_context->info("  Device ID           = {:08x}",      properties.deviceID);
    log_context->info("  Device type         = {}",          c_str(properties.deviceType));
    log_context->info("  Device name         = {}",          properties.deviceName);
    log_context->info("  maxMultiviewViewCount     = {}",    query_multiview_properties.maxMultiviewViewCount);
    log_context->info("  maxMultiviewInstanceIndex = {}",    query_multiview_properties.maxMultiviewInstanceIndex);
    // Detach m_portability_subset_properties from the local property chain
    // so it stays a self-contained struct for downstream queries.
    m_portability_subset_properties.pNext = nullptr;
    if (m_device_extensions.m_VK_KHR_portability_subset) {
        log_context->info("Vulkan portability subset properties:");
        log_context->info(
            "  minVertexInputBindingStrideAlignment = {}",
            m_portability_subset_properties.minVertexInputBindingStrideAlignment
        );
    }

    VkPhysicalDeviceFeatures2 query_device_features{
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = nullptr,
        .features = VkPhysicalDeviceFeatures{}
    };
    VkBaseOutStructure* query_features_chain_last = reinterpret_cast<VkBaseOutStructure*>(&query_device_features);

    VkPhysicalDeviceTimelineSemaphoreFeatures query_timeline_semaphore_features = {
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext             = nullptr,
        .timelineSemaphore = VK_FALSE
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_timeline_semaphore_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR query_present_mode_fifo_latest_ready_features{
        .sType                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext                      = nullptr,
        .presentModeFifoLatestReady = VK_FALSE
    };
    m_capabilities.m_present_mode_fifo_latest_ready =
        m_device_extensions.m_VK_KHR_present_mode_fifo_latest_ready ||
        m_device_extensions.m_VK_EXT_present_mode_fifo_latest_ready;
    if (m_capabilities.m_present_mode_fifo_latest_ready) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_mode_fifo_latest_ready_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    // Frame pacing feature queries (implementation plan step P0.1).
    VkPhysicalDevicePresentIdFeaturesKHR query_present_id_features{
        .sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
        .pNext     = nullptr,
        .presentId = VK_FALSE
    };
    if (m_device_extensions.m_VK_KHR_present_id) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_id_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentWaitFeaturesKHR query_present_wait_features{
        .sType       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
        .pNext       = nullptr,
        .presentWait = VK_FALSE
    };
    if (m_device_extensions.m_VK_KHR_present_wait) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_wait_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentId2FeaturesKHR query_present_id2_features{
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR,
        .pNext      = nullptr,
        .presentId2 = VK_FALSE
    };
    if (m_device_extensions.m_VK_KHR_present_id2) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_id2_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentWait2FeaturesKHR query_present_wait2_features{
        .sType        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR,
        .pNext        = nullptr,
        .presentWait2 = VK_FALSE
    };
    if (m_device_extensions.m_VK_KHR_present_wait2) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_wait2_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentTimingFeaturesEXT query_present_timing_features{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT,
        .pNext                 = nullptr,
        .presentTiming         = VK_FALSE,
        .presentAtAbsoluteTime = VK_FALSE,
        .presentAtRelativeTime = VK_FALSE
    };
    if (m_device_extensions.m_VK_EXT_present_timing) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_present_timing_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }
    // Host query reset (core 1.2): backs the frame-bracket query ring
    // (implementation plan step P0.3) - reset from the CPU in wait_frame
    // without needing a command buffer.
    VkPhysicalDeviceHostQueryResetFeatures query_host_query_reset_features{
        .sType          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .pNext          = nullptr,
        .hostQueryReset = VK_FALSE
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_host_query_reset_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    m_capabilities.m_surface_maintenance1 =
        m_instance_extensions.m_VK_KHR_surface_maintenance1 ||
        m_instance_extensions.m_VK_EXT_surface_maintenance1;

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT query_swapchain_maintenance_features{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .pNext                 = nullptr,
        .swapchainMaintenance1 = VK_FALSE,
    };
    m_capabilities.m_swapchain_maintenance1 =
        m_device_extensions.m_VK_KHR_swapchain_maintenance1 ||
        m_device_extensions.m_VK_EXT_swapchain_maintenance1;
    if (m_capabilities.m_swapchain_maintenance1) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_swapchain_maintenance_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDeviceDescriptorIndexingFeatures query_descriptor_indexing_features{
        .sType                                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext                                              = nullptr,
        .shaderInputAttachmentArrayDynamicIndexing          = VK_FALSE,
        .shaderUniformTexelBufferArrayDynamicIndexing       = VK_FALSE,
        .shaderStorageTexelBufferArrayDynamicIndexing       = VK_FALSE,
        .shaderUniformBufferArrayNonUniformIndexing         = VK_FALSE,
        .shaderSampledImageArrayNonUniformIndexing          = VK_FALSE,
        .shaderStorageBufferArrayNonUniformIndexing         = VK_FALSE,
        .shaderStorageImageArrayNonUniformIndexing          = VK_FALSE,
        .shaderInputAttachmentArrayNonUniformIndexing       = VK_FALSE,
        .shaderUniformTexelBufferArrayNonUniformIndexing    = VK_FALSE,
        .shaderStorageTexelBufferArrayNonUniformIndexing    = VK_FALSE,
        .descriptorBindingUniformBufferUpdateAfterBind      = VK_FALSE,
        .descriptorBindingSampledImageUpdateAfterBind       = VK_FALSE,
        .descriptorBindingStorageImageUpdateAfterBind       = VK_FALSE,
        .descriptorBindingStorageBufferUpdateAfterBind      = VK_FALSE,
        .descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingStorageTexelBufferUpdateAfterBind = VK_FALSE,
        .descriptorBindingUpdateUnusedWhilePending          = VK_FALSE,
        .descriptorBindingPartiallyBound                    = VK_FALSE,
        .descriptorBindingVariableDescriptorCount           = VK_FALSE,
        .runtimeDescriptorArray                             = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_descriptor_indexing_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDeviceVulkan11Features query_vulkan_11_features{
        .sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext                              = nullptr,
        .storageBuffer16BitAccess           = VK_FALSE,
        .uniformAndStorageBuffer16BitAccess = VK_FALSE,
        .storagePushConstant16              = VK_FALSE,
        .storageInputOutput16               = VK_FALSE,
        .multiview                          = VK_FALSE,
        .multiviewGeometryShader            = VK_FALSE,
        .multiviewTessellationShader        = VK_FALSE,
        .variablePointersStorageBuffer      = VK_FALSE,
        .variablePointers                   = VK_FALSE,
        .protectedMemory                    = VK_FALSE,
        .samplerYcbcrConversion             = VK_FALSE,
        .shaderDrawParameters               = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_vulkan_11_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    VkPhysicalDeviceVulkan13Features query_vulkan_13_features{
        .sType                                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext                                              = nullptr,
        .robustImageAccess                                  = VK_FALSE,
        .inlineUniformBlock                                 = VK_FALSE,
        .descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE,
        .pipelineCreationCacheControl                       = VK_FALSE,
        .privateData                                        = VK_FALSE,
        .shaderDemoteToHelperInvocation                     = VK_FALSE,
        .shaderTerminateInvocation                          = VK_FALSE,
        .subgroupSizeControl                                = VK_FALSE,
        .computeFullSubgroups                               = VK_FALSE,
        .synchronization2                                   = VK_FALSE,
        .textureCompressionASTC_HDR                         = VK_FALSE,
        .shaderZeroInitializeWorkgroupMemory                = VK_FALSE,
        .dynamicRendering                                   = VK_FALSE,
        .shaderIntegerDotProduct                            = VK_FALSE,
        .maintenance4                                       = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_vulkan_13_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    // Promoted to core in Vulkan 1.2; the extension struct is still a
    // valid alias of VkPhysicalDeviceVulkan12Features::shaderFloat16 /
    // shaderInt8 on 1.2+ devices, so we can chain it unconditionally.
    VkPhysicalDeviceShaderFloat16Int8Features query_shader_float16_int8_features{
        .sType         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
        .pNext         = nullptr,
        .shaderFloat16 = VK_FALSE,
        .shaderInt8    = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_shader_float16_int8_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    // VK_KHR_16bit_storage, promoted to Vulkan 1.1 core. Aliases
    // VkPhysicalDeviceVulkan11Features bits of the same name; safe to
    // chain unconditionally on a 1.1+ device.
    VkPhysicalDevice16BitStorageFeatures query_16bit_storage_features{
        .sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
        .pNext                              = nullptr,
        .storageBuffer16BitAccess           = VK_FALSE,
        .uniformAndStorageBuffer16BitAccess = VK_FALSE,
        .storagePushConstant16              = VK_FALSE,
        .storageInputOutput16               = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_16bit_storage_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    // VK_KHR_multiview, promoted to Vulkan 1.1 core. Aliases
    // VkPhysicalDeviceVulkan11Features bits of the same name; safe to
    // chain unconditionally on a 1.1+ device.
    VkPhysicalDeviceMultiviewFeatures query_multiview_features{
        .sType                        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES,
        .pNext                        = nullptr,
        .multiview                    = VK_FALSE,
        .multiviewGeometryShader      = VK_FALSE,
        .multiviewTessellationShader  = VK_FALSE,
    };
    {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_multiview_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    // VK_EXT_fragment_density_map (OpenXR fixed foveated rendering). Unlike the
    // 1.1-core features above this is a non-core extension, so the feature struct
    // is chained only when the device advertises the extension.
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT query_fragment_density_map_features{
        .sType                                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT,
        .pNext                                 = nullptr,
        .fragmentDensityMap                    = VK_FALSE,
        .fragmentDensityMapDynamic             = VK_FALSE,
        .fragmentDensityMapNonSubsampledImages = VK_FALSE,
    };
    if (m_device_extensions.m_VK_EXT_fragment_density_map) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&query_fragment_density_map_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    // When VK_KHR_portability_subset is NOT advertised the device is fully
    // featured by definition, so we synthesise an all-VK_TRUE struct here.
    // When it IS advertised we chain into the features2 query and the driver
    // overwrites the per-bit defaults below.
    m_portability_subset_features = VkPhysicalDevicePortabilitySubsetFeaturesKHR{
        .sType                                  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR,
        .pNext                                  = nullptr,
        .constantAlphaColorBlendFactors         = VK_TRUE,
        .events                                 = VK_TRUE,
        .imageViewFormatReinterpretation        = VK_TRUE,
        .imageViewFormatSwizzle                 = VK_TRUE,
        .imageView2DOn3DImage                   = VK_TRUE,
        .multisampleArrayImage                  = VK_TRUE,
        .mutableComparisonSamplers              = VK_TRUE,
        .pointPolygons                          = VK_TRUE,
        .samplerMipLodBias                      = VK_TRUE,
        .separateStencilMaskRef                 = VK_TRUE,
        .shaderSampleRateInterpolationFunctions = VK_TRUE,
        .tessellationIsolines                   = VK_TRUE,
        .tessellationPointMode                  = VK_TRUE,
        .triangleFans                           = VK_TRUE,
        .vertexAttributeAccessBeyondStride      = VK_TRUE,
    };
    if (m_device_extensions.m_VK_KHR_portability_subset) {
        query_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&m_portability_subset_features);
        query_features_chain_last        = query_features_chain_last->pNext;
    }

    vkGetPhysicalDeviceFeatures2(m_vulkan_physical_device, &query_device_features);

    // Frame pacing capability tier resolution
    // (doc/frame_pacing_capability_tiers.md section 2). Feature-confirmed:
    // extension presence alone is not enough.
    m_capabilities.m_present_id =
        (query_present_id_features.presentId   == VK_TRUE) ||
        (query_present_id2_features.presentId2 == VK_TRUE);
    m_capabilities.m_present_wait =
        (query_present_wait_features.presentWait   == VK_TRUE) ||
        (query_present_wait2_features.presentWait2 == VK_TRUE);
    m_capabilities.m_present_timing =
        (query_present_timing_features.presentTiming == VK_TRUE) &&
        (query_present_timing_features.presentAtAbsoluteTime == VK_TRUE);
    m_capabilities.m_present_at_absolute_time = query_present_timing_features.presentAtAbsoluteTime == VK_TRUE;
    m_capabilities.m_present_at_relative_time = query_present_timing_features.presentAtRelativeTime == VK_TRUE;
    m_capabilities.m_calibrated_timestamps =
        m_device_extensions.m_VK_KHR_calibrated_timestamps ||
        m_device_extensions.m_VK_EXT_calibrated_timestamps;
    m_host_query_reset = query_host_query_reset_features.hostQueryReset == VK_TRUE;
    m_capabilities.m_frame_pacing_tier_w =
        m_capabilities.m_present_id            &&
        m_capabilities.m_present_wait          &&
        m_capabilities.m_present_timing        &&
        m_capabilities.m_calibrated_timestamps;
    log_context->info("Frame pacing capabilities:");
    log_context->info("  present_id            = {}", m_capabilities.m_present_id);
    log_context->info("  present_wait          = {}", m_capabilities.m_present_wait);
    log_context->info("  present_timing        = {}", m_capabilities.m_present_timing);
    log_context->info("  calibrated_timestamps = {}", m_capabilities.m_calibrated_timestamps);

    // Tier resolution (P4.2): capabilities + the frame_pacing_tier config.
    // Must happen HERE, before the surface picks its present mode below
    // (use_physical_device -> choose_present_mode): the resolved tier owns
    // that decision - tier W wants fifo_latest_ready + present timing,
    // tier S wants plain FIFO + minimum image count (backpressure for the
    // slop servo, claim C21a).
    {
        const std::string& requested = get_graphics_config().frame_pacing_tier;
        if (requested == "off") {
            m_frame_pacing_tier = Frame_pacing_tier::off;
        } else if (requested == "s") {
            m_frame_pacing_tier = Frame_pacing_tier::slop_servo;
        } else if (requested == "w") {
            if (m_capabilities.m_frame_pacing_tier_w) {
                m_frame_pacing_tier = Frame_pacing_tier::full;
            } else {
                log_context->warn("frame_pacing_tier 'w' forced but tier W capabilities are missing; using tier S");
                m_frame_pacing_tier = Frame_pacing_tier::slop_servo;
            }
        } else {
            if (requested != "auto") {
                log_context->warn("frame_pacing_tier '{}' not recognized; using 'auto'", requested);
            }
            // Plain FIFO is mandatory in Vulkan, so tier S is always
            // available as the fallback.
            m_frame_pacing_tier = m_capabilities.m_frame_pacing_tier_w
                ? Frame_pacing_tier::full
                : Frame_pacing_tier::slop_servo;
        }
    }
    log_context->info(
        "  frame pacing tier     = {}",
        (m_frame_pacing_tier == Frame_pacing_tier::full)       ? "W (full pacing)" :
        (m_frame_pacing_tier == Frame_pacing_tier::slop_servo) ? "S (slop-servo fallback)" :
                                                                 "OFF"
    );

    // Detach m_portability_subset_features from the local feature chain so
    // its pNext doesn't dangle after locals here go out of scope.
    m_portability_subset_features.pNext = nullptr;

    if (m_device_extensions.m_VK_KHR_portability_subset) {
        const VkPhysicalDevicePortabilitySubsetFeaturesKHR& p = m_portability_subset_features;
        log_context->info("Vulkan portability subset features:");
        log_context->info("  constantAlphaColorBlendFactors         = {}", p.constantAlphaColorBlendFactors         == VK_TRUE);
        log_context->info("  events                                 = {}", p.events                                 == VK_TRUE);
        log_context->info("  imageViewFormatReinterpretation        = {}", p.imageViewFormatReinterpretation        == VK_TRUE);
        log_context->info("  imageViewFormatSwizzle                 = {}", p.imageViewFormatSwizzle                 == VK_TRUE);
        log_context->info("  imageView2DOn3DImage                   = {}", p.imageView2DOn3DImage                   == VK_TRUE);
        log_context->info("  multisampleArrayImage                  = {}", p.multisampleArrayImage                  == VK_TRUE);
        log_context->info("  mutableComparisonSamplers              = {}", p.mutableComparisonSamplers              == VK_TRUE);
        log_context->info("  pointPolygons                          = {}", p.pointPolygons                          == VK_TRUE);
        log_context->info("  samplerMipLodBias                      = {}", p.samplerMipLodBias                      == VK_TRUE);
        log_context->info("  separateStencilMaskRef                 = {}", p.separateStencilMaskRef                 == VK_TRUE);
        log_context->info("  shaderSampleRateInterpolationFunctions = {}", p.shaderSampleRateInterpolationFunctions == VK_TRUE);
        log_context->info("  tessellationIsolines                   = {}", p.tessellationIsolines                   == VK_TRUE);
        log_context->info("  tessellationPointMode                  = {}", p.tessellationPointMode                  == VK_TRUE);
        log_context->info("  triangleFans                           = {}", p.triangleFans                           == VK_TRUE);
        log_context->info("  vertexAttributeAccessBeyondStride      = {}", p.vertexAttributeAccessBeyondStride      == VK_TRUE);
    }

    bool debug_callback_registered = false;
    if (m_instance_extensions.m_VK_EXT_debug_utils) {
        VkDebugUtilsMessageTypeFlagsEXT message_types =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        const VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info{
            .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext           = nullptr,
            .flags           = 0,
            .messageSeverity =
                // Verbose/info severities produce a steady stream of driver-
                // side chatter even without validation layers (e.g. MoltenVK
                // emits "Created 3 swapchain images..." on every swapchain
                // recreation, which on live resize fires per frame). Re-enable
                // them if you need loader-level diagnostics.
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType     = message_types,
            .pfnUserCallback = Device_impl_debug_utils_messenger_callback,
            .pUserData       = static_cast<void*>(this)
        };
        result = vkCreateDebugUtilsMessengerEXT(
            m_vulkan_instance,
            &debug_utils_messenger_create_info,
            nullptr,
            &m_debug_utils_messenger
        );
        if (result == VK_SUCCESS) {
            debug_callback_registered = true;
            Scoped_debug_group_impl::s_enabled = true;
        } else {
            log_context->warn("vkCreateDebugUtilsMessengerEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        }
    }
    if (m_instance_extensions.m_VK_EXT_debug_report && !debug_callback_registered) {
        const VkDebugReportCallbackCreateInfoEXT debug_report_callback_create_info{
            .sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
            .pNext       = nullptr,
            .flags       =
                VK_DEBUG_REPORT_INFORMATION_BIT_EXT         |
                VK_DEBUG_REPORT_WARNING_BIT_EXT             |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT               |
                VK_DEBUG_REPORT_DEBUG_BIT_EXT,
            .pfnCallback = Device_impl_debug_report_callback,
            .pUserData   = static_cast<void*>(this),
        };
        result = vkCreateDebugReportCallbackEXT(
            m_vulkan_instance,
            &debug_report_callback_create_info,
            nullptr,
            &m_debug_report_callback
        );
        if (result != VK_SUCCESS) {
            log_context->warn("vkCreateDebugReportCallbackEXT() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        }
    }

    const float queue_priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_create_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .queueFamilyIndex = m_graphics_queue_family_index,
        .queueCount       = 1,
        .pQueuePriorities = &queue_priority,
    };

    // Validate required features
    const VkPhysicalDeviceFeatures& qf = query_device_features.features;
#if defined(ERHE_OS_ANDROID)
    // Mobile Vulkan drivers (e.g. Mali-G715) commonly lack
    // shaderClipDistance and vertexPipelineStoresAndAtomics. Phase 2 of the
    // Android port keeps these soft so device init completes; downstream
    // pipelines that depend on them (the ImGui vertex shader uses
    // gl_ClipDistance) will need their own fallback path.
    if (qf.shaderClipDistance == VK_FALSE) {
        log_startup->warn("Vulkan feature shaderClipDistance is not supported - ImGui clipping will be disabled");
    }
    if (qf.vertexPipelineStoresAndAtomics == VK_FALSE) {
        log_startup->warn("Vulkan feature vertexPipelineStoresAndAtomics is not supported");
    }
#else
    if (qf.shaderClipDistance == VK_FALSE) {
        log_startup->critical("Required Vulkan feature shaderClipDistance is not supported");
        abort();
    }
    if (qf.vertexPipelineStoresAndAtomics == VK_FALSE) {
        log_startup->critical("Required Vulkan feature vertexPipelineStoresAndAtomics is not supported");
        abort();
    }
#endif
    if (query_vulkan_13_features.synchronization2 == VK_FALSE) {
        log_startup->critical("Required Vulkan 1.3 feature synchronization2 is not supported");
        abort();
    }

    // Log optional feature availability
    log_startup->info("Vulkan device features:");
    log_startup->info("  vertexPipelineStoresAndAtomics = {}", qf.vertexPipelineStoresAndAtomics == VK_TRUE);
    log_startup->info("  multiDrawIndirect              = {}", qf.multiDrawIndirect  == VK_TRUE);
    log_startup->info("  samplerAnisotropy              = {}", qf.samplerAnisotropy  == VK_TRUE);
    log_startup->info("  shaderClipDistance             = {}", qf.shaderClipDistance == VK_TRUE);
    log_startup->info("  shaderCullDistance             = {}", qf.shaderCullDistance == VK_TRUE);
    log_startup->info("  shaderInt64                    = {}", qf.shaderInt64        == VK_TRUE);

    VkPhysicalDeviceFeatures2 set_device_features{
        .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext    = nullptr,
        .features = {
            .robustBufferAccess                      = VK_FALSE,
            .fullDrawIndexUint32                     = VK_FALSE,
            // erhe samples cube-map-array textures (e.g. the point-light cube
            // shadow maps) via samplerCubeArray, whose SPIR-V declares the
            // SampledCubeArray capability. That capability requires the
            // imageCubeArray device feature to be enabled, else shaders trip
            // VUID-VkShaderModuleCreateInfo-pCode-08740. Desktop drivers do not
            // enforce it; KosmicKrisp / the validation layer do.
            .imageCubeArray                          = qf.imageCubeArray,
            .independentBlend                        = VK_FALSE,
            .geometryShader                          = qf.geometryShader,
            .tessellationShader                      = VK_FALSE,
            .sampleRateShading                       = qf.sampleRateShading,
            .dualSrcBlend                            = VK_FALSE,
            .logicOp                                 = VK_FALSE,
            .multiDrawIndirect                       = qf.multiDrawIndirect,
            .drawIndirectFirstInstance               = VK_FALSE,
            .depthClamp                              = qf.depthClamp,
            .depthBiasClamp                          = VK_FALSE,
            .fillModeNonSolid                        = VK_FALSE,
            .depthBounds                             = VK_FALSE,
            .wideLines                               = VK_FALSE,
            .largePoints                             = VK_FALSE,
            .alphaToOne                              = VK_FALSE,
            .multiViewport                           = VK_FALSE,
            .samplerAnisotropy                       = qf.samplerAnisotropy,
            .textureCompressionETC2                  = VK_FALSE,
            .textureCompressionASTC_LDR              = VK_FALSE,
            .textureCompressionBC                    = VK_FALSE,
            .occlusionQueryPrecise                   = VK_FALSE,
            .pipelineStatisticsQuery                 = VK_FALSE,
            .vertexPipelineStoresAndAtomics          = qf.vertexPipelineStoresAndAtomics,
            .fragmentStoresAndAtomics                = VK_FALSE,
            .shaderTessellationAndGeometryPointSize  = VK_FALSE,
            .shaderImageGatherExtended               = VK_FALSE,
            .shaderStorageImageExtendedFormats       = VK_FALSE,
            // OpenXR runtimes (e.g. SteamVR) can inject internal compositor
            // shaders through xrCreateVulkanDeviceKHR that declare the
            // StorageImageMultisample SPIR-V capability. Enable the feature
            // when the physical device supports it so those shaders pass
            // VUID-VkShaderModuleCreateInfo-pCode-08740.
            .shaderStorageImageMultisample           = qf.shaderStorageImageMultisample,
            .shaderStorageImageReadWithoutFormat     = VK_FALSE,
            .shaderStorageImageWriteWithoutFormat    = VK_FALSE,
            .shaderUniformBufferArrayDynamicIndexing = VK_FALSE,
            .shaderSampledImageArrayDynamicIndexing  = VK_FALSE,
            .shaderStorageBufferArrayDynamicIndexing = VK_FALSE,
            .shaderStorageImageArrayDynamicIndexing  = VK_FALSE,
            .shaderClipDistance                      = qf.shaderClipDistance,
            .shaderCullDistance                      = qf.shaderCullDistance,
            .shaderFloat64                           = VK_FALSE,
            .shaderInt64                             = qf.shaderInt64,
            .shaderInt16                             = VK_FALSE,
            .shaderResourceResidency                 = VK_FALSE,
            .shaderResourceMinLod                    = VK_FALSE,
            .sparseBinding                           = VK_FALSE,
            .sparseResidencyBuffer                   = VK_FALSE,
            .sparseResidencyImage2D                  = VK_FALSE,
            .sparseResidencyImage3D                  = VK_FALSE,
            .sparseResidency2Samples                 = VK_FALSE,
            .sparseResidency4Samples                 = VK_FALSE,
            .sparseResidency8Samples                 = VK_FALSE,
            .sparseResidency16Samples                = VK_FALSE,
            .sparseResidencyAliased                  = VK_FALSE,
            .variableMultisampleRate                 = VK_FALSE,
            .inheritedQueries                        = VK_FALSE,
        }
    };
    VkBaseOutStructure* set_features_chain_last = reinterpret_cast<VkBaseOutStructure*>(&set_device_features);

    VkPhysicalDeviceTimelineSemaphoreFeatures set_timeline_semaphore_features = {
        .sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext             = nullptr,
        .timelineSemaphore = query_timeline_semaphore_features.timelineSemaphore
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_timeline_semaphore_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT set_swapchain_maintenance_features{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .pNext                 = nullptr,
        .swapchainMaintenance1 = query_swapchain_maintenance_features.swapchainMaintenance1
    };
    if (m_capabilities.m_swapchain_maintenance1) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_swapchain_maintenance_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_swapchain_maintenance_features.swapchainMaintenance1 == VK_TRUE) {
            log_debug->debug("Enabled feature swapchain maintenance1");
        }
    }
    VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR set_present_mode_fifo_latest_ready_features{
        .sType                      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_MODE_FIFO_LATEST_READY_FEATURES_KHR,
        .pNext                      = nullptr,
        .presentModeFifoLatestReady = query_present_mode_fifo_latest_ready_features.presentModeFifoLatestReady
    };
    if (m_capabilities.m_present_mode_fifo_latest_ready) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_mode_fifo_latest_ready_features);
        set_features_chain_last = set_features_chain_last->pNext;
        if (query_present_mode_fifo_latest_ready_features.presentModeFifoLatestReady == VK_TRUE) {
            log_debug->debug("Enabled feature present mode fifo latest ready");
        }
    }

    // Frame pacing feature enables (implementation plan step P0.1): enable
    // exactly what the query reported for each probed extension.
    VkPhysicalDevicePresentIdFeaturesKHR set_present_id_features{
        .sType     = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
        .pNext     = nullptr,
        .presentId = query_present_id_features.presentId
    };
    if (m_device_extensions.m_VK_KHR_present_id) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_id_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentWaitFeaturesKHR set_present_wait_features{
        .sType       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
        .pNext       = nullptr,
        .presentWait = query_present_wait_features.presentWait
    };
    if (m_device_extensions.m_VK_KHR_present_wait) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_wait_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentId2FeaturesKHR set_present_id2_features{
        .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_2_FEATURES_KHR,
        .pNext      = nullptr,
        .presentId2 = query_present_id2_features.presentId2
    };
    if (m_device_extensions.m_VK_KHR_present_id2) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_id2_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentWait2FeaturesKHR set_present_wait2_features{
        .sType        = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR,
        .pNext        = nullptr,
        .presentWait2 = query_present_wait2_features.presentWait2
    };
    if (m_device_extensions.m_VK_KHR_present_wait2) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_wait2_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }
    VkPhysicalDevicePresentTimingFeaturesEXT set_present_timing_features{
        .sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_TIMING_FEATURES_EXT,
        .pNext                 = nullptr,
        .presentTiming         = query_present_timing_features.presentTiming,
        .presentAtAbsoluteTime = query_present_timing_features.presentAtAbsoluteTime,
        .presentAtRelativeTime = query_present_timing_features.presentAtRelativeTime
    };
    if (m_device_extensions.m_VK_EXT_present_timing) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_present_timing_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_present_timing_features.presentTiming == VK_TRUE) {
            log_debug->debug("Enabled feature present timing");
        }
    }
    VkPhysicalDeviceHostQueryResetFeatures set_host_query_reset_features{
        .sType          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .pNext          = nullptr,
        .hostQueryReset = query_host_query_reset_features.hostQueryReset
    };
    if (m_host_query_reset) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_host_query_reset_features);
        set_features_chain_last        = set_features_chain_last->pNext;
    }

    VkPhysicalDeviceDescriptorIndexingFeatures set_descriptor_indexing_features{
        .sType                                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .pNext                                              = nullptr,
        .shaderInputAttachmentArrayDynamicIndexing          = query_descriptor_indexing_features.shaderInputAttachmentArrayDynamicIndexing,
        .shaderUniformTexelBufferArrayDynamicIndexing       = query_descriptor_indexing_features.shaderUniformTexelBufferArrayDynamicIndexing,
        .shaderStorageTexelBufferArrayDynamicIndexing       = query_descriptor_indexing_features.shaderStorageTexelBufferArrayDynamicIndexing,
        .shaderUniformBufferArrayNonUniformIndexing         = query_descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing,
        .shaderSampledImageArrayNonUniformIndexing          = query_descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing,
        .shaderStorageBufferArrayNonUniformIndexing         = query_descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing,
        .shaderStorageImageArrayNonUniformIndexing          = query_descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing,
        .shaderInputAttachmentArrayNonUniformIndexing       = query_descriptor_indexing_features.shaderInputAttachmentArrayNonUniformIndexing,
        .shaderUniformTexelBufferArrayNonUniformIndexing    = query_descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing,
        .shaderStorageTexelBufferArrayNonUniformIndexing    = query_descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing,
        .descriptorBindingUniformBufferUpdateAfterBind      = query_descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind,
        .descriptorBindingSampledImageUpdateAfterBind       = query_descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind,
        .descriptorBindingStorageImageUpdateAfterBind       = query_descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind,
        .descriptorBindingStorageBufferUpdateAfterBind      = query_descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind,
        .descriptorBindingUniformTexelBufferUpdateAfterBind = query_descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind,
        .descriptorBindingStorageTexelBufferUpdateAfterBind = query_descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind,
        .descriptorBindingUpdateUnusedWhilePending          = query_descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending,
        .descriptorBindingPartiallyBound                    = query_descriptor_indexing_features.descriptorBindingPartiallyBound,
        .descriptorBindingVariableDescriptorCount           = query_descriptor_indexing_features.descriptorBindingVariableDescriptorCount,
        .runtimeDescriptorArray                             = query_descriptor_indexing_features.runtimeDescriptorArray,
    };
    {
        // Validate required descriptor indexing features for texture heap
        const VkPhysicalDeviceDescriptorIndexingFeatures& di = query_descriptor_indexing_features;
        if (di.runtimeDescriptorArray == VK_FALSE) {
            log_startup->critical("Required Vulkan feature runtimeDescriptorArray is not supported");
            abort();
        }
        if (di.descriptorBindingPartiallyBound == VK_FALSE) {
            log_startup->critical("Required Vulkan feature descriptorBindingPartiallyBound is not supported");
            abort();
        }
        if (di.descriptorBindingVariableDescriptorCount == VK_FALSE) {
            log_startup->critical("Required Vulkan feature descriptorBindingVariableDescriptorCount is not supported");
            abort();
        }

        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_descriptor_indexing_features);
        set_features_chain_last        = set_features_chain_last->pNext ;
        log_startup->info("  runtimeDescriptorArray                       = {}", di.runtimeDescriptorArray == VK_TRUE);
        log_startup->info("  descriptorBindingPartiallyBound              = {}", di.descriptorBindingPartiallyBound == VK_TRUE);
        log_startup->info("  descriptorBindingVariableDescriptorCount     = {}", di.descriptorBindingVariableDescriptorCount == VK_TRUE);
        log_startup->info("  descriptorBindingSampledImageUpdateAfterBind = {}", di.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE);
    }

    // VkPhysicalDeviceVulkan11Features subsumes VkPhysicalDevice16BitStorageFeatures
    // and VkPhysicalDeviceMultiviewFeatures (and several other 1.1-promoted
    // feature structs). Per VUID-VkDeviceCreateInfo-pNext-02829, the chain
    // must not include both the 1.1 Features struct AND any of the granular
    // structs it subsumes - the spec disallows aliasing the same feature
    // through two different paths. So we set the 16bit-storage and multiview
    // flags directly on this struct and skip pushing the granular versions
    // onto the chain below.
    VkPhysicalDeviceVulkan11Features set_vulkan_11_features{
        .sType                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext                              = nullptr,
        .storageBuffer16BitAccess           = query_16bit_storage_features.storageBuffer16BitAccess,
        .uniformAndStorageBuffer16BitAccess = query_16bit_storage_features.uniformAndStorageBuffer16BitAccess,
        .storagePushConstant16              = query_16bit_storage_features.storagePushConstant16,
        .storageInputOutput16               = query_16bit_storage_features.storageInputOutput16,
        .multiview                          = query_multiview_features.multiview,
        .multiviewGeometryShader            = query_multiview_features.multiviewGeometryShader,
        .multiviewTessellationShader        = query_multiview_features.multiviewTessellationShader,
        .variablePointersStorageBuffer      = VK_FALSE,
        .variablePointers                   = VK_FALSE,
        .protectedMemory                    = VK_FALSE,
        .samplerYcbcrConversion             = VK_FALSE,
        .shaderDrawParameters               = query_vulkan_11_features.shaderDrawParameters,
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_vulkan_11_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_vulkan_11_features.storageBuffer16BitAccess           == VK_TRUE) log_debug->debug("Enabled feature storageBuffer16BitAccess");
        if (set_vulkan_11_features.uniformAndStorageBuffer16BitAccess == VK_TRUE) log_debug->debug("Enabled feature uniformAndStorageBuffer16BitAccess");
        if (set_vulkan_11_features.storagePushConstant16              == VK_TRUE) log_debug->debug("Enabled feature storagePushConstant16");
        if (set_vulkan_11_features.storageInputOutput16               == VK_TRUE) log_debug->debug("Enabled feature storageInputOutput16");
        if (set_vulkan_11_features.multiview                          == VK_TRUE) log_debug->debug("Enabled feature multiview");
        if (set_vulkan_11_features.multiviewGeometryShader            == VK_TRUE) log_debug->debug("Enabled feature multiviewGeometryShader");
        if (set_vulkan_11_features.multiviewTessellationShader        == VK_TRUE) log_debug->debug("Enabled feature multiviewTessellationShader");
        if (set_vulkan_11_features.shaderDrawParameters               == VK_TRUE) log_debug->debug("Enabled feature shaderDrawParameters");
    }

    VkPhysicalDeviceVulkan13Features set_vulkan_13_features{
        .sType                                              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext                                              = nullptr,
        .robustImageAccess                                  = VK_FALSE,
        .inlineUniformBlock                                 = VK_FALSE,
        .descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE,
        .pipelineCreationCacheControl                       = VK_FALSE,
        .privateData                                        = VK_FALSE,
        .shaderDemoteToHelperInvocation                     = query_vulkan_13_features.shaderDemoteToHelperInvocation,
        .shaderTerminateInvocation                          = VK_FALSE,
        .subgroupSizeControl                                = VK_FALSE,
        .computeFullSubgroups                               = VK_FALSE,
        .synchronization2                                   = query_vulkan_13_features.synchronization2,
        .textureCompressionASTC_HDR                         = VK_FALSE,
        .shaderZeroInitializeWorkgroupMemory                = VK_FALSE,
        .dynamicRendering                                   = query_vulkan_13_features.dynamicRendering,
        .shaderIntegerDotProduct                            = VK_FALSE,
        .maintenance4                                       = VK_FALSE,
    };
    {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_vulkan_13_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_vulkan_13_features.synchronization2 == VK_TRUE) {
            log_debug->debug("Enabled feature synchronization2");
        }
        if (set_vulkan_13_features.dynamicRendering == VK_TRUE) {
            log_debug->debug("Enabled feature dynamicRendering");
        }
    }

    VkPhysicalDeviceShaderFloat16Int8Features set_shader_float16_int8_features{
        .sType         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
        .pNext         = nullptr,
        .shaderFloat16 = query_shader_float16_int8_features.shaderFloat16,
        .shaderInt8    = query_shader_float16_int8_features.shaderInt8,
    };
    if ((set_shader_float16_int8_features.shaderFloat16 == VK_TRUE) ||
        (set_shader_float16_int8_features.shaderInt8    == VK_TRUE)) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_shader_float16_int8_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        if (set_shader_float16_int8_features.shaderFloat16 == VK_TRUE) {
            log_debug->debug("Enabled feature shaderFloat16");
        }
        if (set_shader_float16_int8_features.shaderInt8 == VK_TRUE) {
            log_debug->debug("Enabled feature shaderInt8");
        }
    }
    log_startup->info("  shaderFloat16                  = {}", query_shader_float16_int8_features.shaderFloat16 == VK_TRUE);
    log_startup->info("  shaderInt8                     = {}", query_shader_float16_int8_features.shaderInt8    == VK_TRUE);

    // VK_EXT_fragment_density_map: enable exactly the bits the device advertised.
    // Only chain it when the extension is present and fragmentDensityMap is
    // supported; enabling an unsupported feature would fail device creation.
    VkPhysicalDeviceFragmentDensityMapFeaturesEXT set_fragment_density_map_features{
        .sType                                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_DENSITY_MAP_FEATURES_EXT,
        .pNext                                 = nullptr,
        .fragmentDensityMap                    = query_fragment_density_map_features.fragmentDensityMap,
        .fragmentDensityMapDynamic             = query_fragment_density_map_features.fragmentDensityMapDynamic,
        .fragmentDensityMapNonSubsampledImages = query_fragment_density_map_features.fragmentDensityMapNonSubsampledImages,
    };
    if (m_device_extensions.m_VK_EXT_fragment_density_map && (set_fragment_density_map_features.fragmentDensityMap == VK_TRUE)) {
        set_features_chain_last->pNext = reinterpret_cast<VkBaseOutStructure*>(&set_fragment_density_map_features);
        set_features_chain_last        = set_features_chain_last->pNext;
        log_debug->debug("Enabled feature fragmentDensityMap");
        if (set_fragment_density_map_features.fragmentDensityMapNonSubsampledImages == VK_TRUE) {
            log_debug->debug("Enabled feature fragmentDensityMapNonSubsampledImages");
        }
    }
    log_startup->info("  fragmentDensityMap                    = {}", query_fragment_density_map_features.fragmentDensityMap                    == VK_TRUE);
    log_startup->info("  fragmentDensityMapDynamic             = {}", query_fragment_density_map_features.fragmentDensityMapDynamic             == VK_TRUE);
    log_startup->info("  fragmentDensityMapNonSubsampledImages = {}", query_fragment_density_map_features.fragmentDensityMapNonSubsampledImages == VK_TRUE);

    // 16bit storage and multiview feature flags are folded into
    // set_vulkan_11_features above; the granular VkPhysicalDevice16BitStorageFeatures
    // and VkPhysicalDeviceMultiviewFeatures structs would alias the same
    // feature bits and trip VUID-VkDeviceCreateInfo-pNext-02829, so we
    // only log the queried availability here for parity with the other
    // feature reports.
    log_startup->info("  storageBuffer16BitAccess           = {}", query_16bit_storage_features.storageBuffer16BitAccess           == VK_TRUE);
    log_startup->info("  uniformAndStorageBuffer16BitAccess = {}", query_16bit_storage_features.uniformAndStorageBuffer16BitAccess == VK_TRUE);
    log_startup->info("  storagePushConstant16              = {}", query_16bit_storage_features.storagePushConstant16              == VK_TRUE);
    log_startup->info("  storageInputOutput16               = {}", query_16bit_storage_features.storageInputOutput16               == VK_TRUE);
    log_startup->info("  multiview                          = {}", query_multiview_features.multiview                   == VK_TRUE);
    log_startup->info("  multiviewGeometryShader            = {}", query_multiview_features.multiviewGeometryShader     == VK_TRUE);
    log_startup->info("  multiviewTessellationShader        = {}", query_multiview_features.multiviewTessellationShader == VK_TRUE);

    const VkDeviceCreateInfo device_create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &set_device_features,
        .flags                   = 0,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_create_info,
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = nullptr,
        .enabledExtensionCount   = static_cast<uint32_t>(device_extensions_c_str.size()),
        .ppEnabledExtensionNames = device_extensions_c_str.data(),
        .pEnabledFeatures        = nullptr
    };
    if ((m_external_creators != nullptr) && static_cast<bool>(m_external_creators->create_device)) {
        log_context->info("Creating Vulkan device via external (XR) hook");
        result = m_external_creators->create_device(m_vulkan_physical_device, &device_create_info, &m_vulkan_device);
    } else {
        result = vkCreateDevice(m_vulkan_physical_device, &device_create_info, nullptr, &m_vulkan_device);
    }
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateDevice() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    if (surface_impl && !surface_impl->use_physical_device(m_vulkan_physical_device)) {
        log_context->critical("Vulkan surface is not suitable");
        abort();
    }
    m_surface = std::make_unique<Surface>(std::move(surface_impl));

    VmaAllocatorCreateInfo vma_create_info{
        .flags                          = 0,                        // VmaAllocatorCreateFlags flags;
        .physicalDevice                 = m_vulkan_physical_device, // VkPhysicalDevice VMA_NOT_NULL physicalDevice;
        .device                         = m_vulkan_device,          // VkDevice VMA_NOT_NULL device;
        .preferredLargeHeapBlockSize    = 0,
        .pAllocationCallbacks           = nullptr,
        .pDeviceMemoryCallbacks         = nullptr,
        .pHeapSizeLimit                 = nullptr,
        .pVulkanFunctions               = nullptr,
        .instance                       = m_vulkan_instance,
        .vulkanApiVersion               = application_info.apiVersion,
#if VMA_EXTERNAL_MEMORY
        .pTypeExternalMemoryHandleTypes = nullptr
#endif
    };

    VmaVulkanFunctions vma_vulkan_functions{};
    result = vmaImportVulkanFunctionsFromVolk(&vma_create_info, &vma_vulkan_functions);
    if (result != VK_SUCCESS) {
        log_context->critical("vmaImportVulkanFunctionsFromVolk() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    vma_create_info.pVulkanFunctions = &vma_vulkan_functions;

    result = vmaCreateAllocator(&vma_create_info, &m_vma_allocator);
    if (result != VK_SUCCESS) {
        log_context->critical("vmaCreateAllocator() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    vkGetDeviceQueue(m_vulkan_device, m_graphics_queue_family_index, 0, &m_vulkan_graphics_queue);
    if (m_vulkan_graphics_queue == VK_NULL_HANDLE) {
        log_context->critical("vkGetDeviceQueue() returned VK_NULL_HANDLE for graphics queue");
        abort();
    }
    s_device_impl = this;

    if (m_surface && !headless) {
        vkGetDeviceQueue(m_vulkan_device, m_present_queue_family_index, 0, &m_vulkan_present_queue);
        if (m_vulkan_present_queue == VK_NULL_HANDLE) {
            log_context->critical("vkGetDeviceQueue() returned VK_NULL_HANDLE for present queue");
            abort();
        }
    }

    // Create one VkCommandPool per (frame_in_flight_slot, thread_slot)
    // pair, all on the graphics queue family. TRANSIENT_BIT signals the
    // driver that buffers allocated from these pools are short-lived,
    // and we reset each pool wholesale (not per-buffer) when the
    // owning frame-in-flight slot's submit fence completes.
    for (size_t frame_slot = 0; frame_slot < s_number_of_frames_in_flight; ++frame_slot) {
        for (unsigned int thread_slot = 0; thread_slot < s_number_of_thread_slots; ++thread_slot) {
            const VkCommandPoolCreateInfo pool_create_info{
                .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext            = nullptr,
                .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                .queueFamilyIndex = m_graphics_queue_family_index
            };
            Per_thread_command_pool& pool = m_command_pools[frame_slot][thread_slot];
            const VkResult pool_result = vkCreateCommandPool(m_vulkan_device, &pool_create_info, nullptr, &pool.command_pool);
            if (pool_result != VK_SUCCESS) {
                log_context->critical(
                    "vkCreateCommandPool() failed with {} {} for (frame_slot={}, thread_slot={})",
                    static_cast<int32_t>(pool_result), c_str(pool_result), frame_slot, thread_slot
                );
                abort();
            }
            set_debug_label(
                VK_OBJECT_TYPE_COMMAND_POOL,
                reinterpret_cast<uint64_t>(pool.command_pool),
                fmt::format("Device cb pool (frame_slot={}, thread_slot={})", frame_slot, thread_slot).c_str()
            );
        }
    }

    const VkSemaphoreTypeCreateInfo semaphore_type_create_info{
        .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .pNext         = nullptr,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue  = 0
    };

    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphore_type_create_info,
        .flags = 0
    };

    result = vkCreateSemaphore(m_vulkan_device, &semaphore_create_info, nullptr, &m_vulkan_frame_end_semaphore);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }

    m_sync_pool = std::make_unique<Device_sync_pool>(*this);

    m_info.vendor                  = get_vendor(properties.vendorID);
    m_info.glsl_version            = 460;
    m_info.vulkan_api_version      = application_info.apiVersion;
    m_info.vulkan_driver_id        = static_cast<uint32_t>(m_driver_properties.driverID);
    {
        // driverName / driverInfo are fixed-size null-terminated char arrays;
        // drop either field from the title if the driver left it empty.
        const char* const driver_name = m_driver_properties.driverName;
        const char* const driver_info = m_driver_properties.driverInfo;
        const bool has_name = (driver_name[0] != '\0');
        const bool has_info = (driver_info[0] != '\0');
        std::string driver_suffix;
        if (has_name && has_info) {
            driver_suffix = fmt::format(" - {} ({})", driver_name, driver_info);
        } else if (has_name) {
            driver_suffix = fmt::format(" - {}", driver_name);
        } else if (has_info) {
            driver_suffix = fmt::format(" - {}", driver_info);
        }
        m_info.api_info = fmt::format(
            "Vulkan {}.{}.{}{}",
            VK_API_VERSION_MAJOR(application_info.apiVersion),
            VK_API_VERSION_MINOR(application_info.apiVersion),
            VK_API_VERSION_PATCH(application_info.apiVersion),
            driver_suffix
        );
    }
    m_info.use_binary_shaders      = true;
    m_info.use_integer_polygon_ids = true;
    m_info.texture_heap_path       = Texture_heap_path::vulkan_descriptor_indexing;
    m_info.use_sparse_texture      = false;
    const bool has_multi_draw_indirect    = (query_device_features.features.multiDrawIndirect == VK_TRUE);
    const bool has_shader_draw_parameters = (query_vulkan_11_features.shaderDrawParameters == VK_TRUE);
    // MoltenVK advertises shaderDrawParameters = VK_TRUE to stay Vulkan 1.1 conformant, but Metal
    // Shading Language has no equivalent of the SPIR-V DrawIndex built-in, so SPIRV-Cross refuses
    // to translate any shader that uses gl_DrawID ("DrawIndex is not supported in MSL"). Force the
    // push-constant emulation path on MoltenVK regardless of the advertised capability.
    const bool is_moltenvk = (m_driver_properties.driverID == VK_DRIVER_ID_MOLTENVK);
    if (has_multi_draw_indirect && has_shader_draw_parameters && !is_moltenvk) {
        m_info.use_multi_draw_indirect_core = true;
        m_info.emulate_multi_draw_indirect  = false;
        log_startup->info("Multi Draw Indirect: native Vulkan (multiDrawIndirect + shaderDrawParameters)");
    } else {
        m_info.use_multi_draw_indirect_core = false;
        m_info.emulate_multi_draw_indirect  = true;
        log_startup->info(
            "Multi Draw Indirect: emulation via push constants (multiDrawIndirect={}, shaderDrawParameters={}, MoltenVK={})",
            has_multi_draw_indirect, has_shader_draw_parameters, is_moltenvk
        );
    }
    // Mesa KosmicKrisp (Vulkan-on-Metal) rejects OpImageRead from a storage
    // image (imageLoad on a `uniform image2D`) at vkCreateComputePipelines with
    // VK_ERROR_INVALID_SHADER_NV, even though the SPIR-V is valid and the same
    // driver accepts OpImageWrite. Shaders that need to read such an image must
    // sample it instead; see WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ.
    const bool is_kosmickrisp = (m_driver_properties.driverID == VK_DRIVER_ID_MESA_KOSMICKRISP);
    m_info.workaround_no_compute_storage_image_read = is_kosmickrisp;
    if (is_kosmickrisp) {
        log_startup->info("Driver workaround (KosmicKrisp): reading storage-image LUTs via sampler in compute (WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ)");
    }
    m_info.use_multi_draw_indirect_arb = false;
    m_info.use_compute_shader          = true;
    m_info.use_shader_storage_buffers  = true;
    m_info.use_clear_texture           = true;
    m_info.use_texture_view            = true;
    m_info.use_persistent_buffers      = true;
    m_info.use_clip_distance           = (qf.shaderClipDistance == VK_TRUE);
    m_info.shader_float16              = (set_shader_float16_int8_features.shaderFloat16 == VK_TRUE);
    m_info.shader_int8                 = (set_shader_float16_int8_features.shaderInt8    == VK_TRUE);
    m_info.storage_buffer_16bit_access             = (set_vulkan_11_features.storageBuffer16BitAccess           == VK_TRUE);
    m_info.uniform_and_storage_buffer_16bit_access = (set_vulkan_11_features.uniformAndStorageBuffer16BitAccess == VK_TRUE);
    m_info.storage_push_constant_16                = (set_vulkan_11_features.storagePushConstant16              == VK_TRUE);
    m_info.storage_input_output_16                 = (set_vulkan_11_features.storageInputOutput16               == VK_TRUE);
    m_info.multiview                               = (set_vulkan_11_features.multiview                          == VK_TRUE);
    m_info.multiview_geometry_shader               = (set_vulkan_11_features.multiviewGeometryShader            == VK_TRUE);
    m_info.multiview_tessellation_shader           = (set_vulkan_11_features.multiviewTessellationShader        == VK_TRUE);
    m_info.max_multiview_view_count                = query_multiview_properties.maxMultiviewViewCount;
    m_info.max_multiview_instance_index            = query_multiview_properties.maxMultiviewInstanceIndex;
    // Core (non-subsampled) FFR needs both fragmentDensityMap and
    // fragmentDensityMapNonSubsampledImages; gate the capability on both.
    m_info.fragment_density_map                    = (set_fragment_density_map_features.fragmentDensityMap                    == VK_TRUE) &&
                                                     (set_fragment_density_map_features.fragmentDensityMapNonSubsampledImages == VK_TRUE);

    // Vulkan coordinate conventions
    m_info.coordinate_conventions.native_depth_range = erhe::math::Depth_range::zero_to_one;
    m_info.coordinate_conventions.framebuffer_origin = erhe::math::Framebuffer_origin::top_left;
    m_info.coordinate_conventions.clip_space_y_flip  = erhe::math::Clip_space_y_flip::disabled;
    m_info.coordinate_conventions.texture_origin     = erhe::math::Texture_origin::top_left;

    const VkPhysicalDeviceLimits& limits = properties.limits;

    // Vulkan drivers may report UINT32_MAX for "unlimited" limits;
    // cap to INT_MAX so the int fields in Device_info stay positive.
    auto cap = [](uint32_t value) -> int {
        return static_cast<int>(std::min(value, static_cast<uint32_t>(INT_MAX)));
    };

    m_info.max_per_stage_descriptor_samplers         = limits.maxPerStageDescriptorSamplers;
    m_info.max_combined_texture_image_units          = cap(limits.maxDescriptorSetSampledImages);
    m_info.max_uniform_block_size                    = cap(limits.maxUniformBufferRange);
    m_info.max_shader_storage_buffer_bindings        = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_uniform_buffer_bindings               = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_compute_shader_storage_blocks         = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_compute_uniform_blocks                = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_compute_shared_memory_size            = cap(limits.maxComputeSharedMemorySize);
    m_info.max_compute_workgroup_count[0]            = cap(limits.maxComputeWorkGroupCount[0]);
    m_info.max_compute_workgroup_count[1]            = cap(limits.maxComputeWorkGroupCount[1]);
    m_info.max_compute_workgroup_count[2]            = cap(limits.maxComputeWorkGroupCount[2]);
    m_info.max_compute_workgroup_size[0]             = cap(limits.maxComputeWorkGroupSize[0]);
    m_info.max_compute_workgroup_size[1]             = cap(limits.maxComputeWorkGroupSize[1]);
    m_info.max_compute_workgroup_size[2]             = cap(limits.maxComputeWorkGroupSize[2]);
    m_info.max_compute_work_group_invocations        = cap(limits.maxComputeWorkGroupInvocations);
    m_info.max_vertex_shader_storage_blocks          = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_vertex_uniform_blocks                 = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_fragment_shader_storage_blocks        = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_fragment_uniform_blocks               = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_geometry_shader_storage_blocks        = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_geometry_uniform_blocks               = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_tess_control_shader_storage_blocks    = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_tess_control_uniform_blocks           = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_tess_evaluation_shader_storage_blocks = cap(limits.maxPerStageDescriptorStorageBuffers);
    m_info.max_tess_evaluation_uniform_blocks        = cap(limits.maxPerStageDescriptorUniformBuffers);
    m_info.max_vertex_attribs                        = cap(limits.maxVertexInputAttributes);
    m_info.max_samples                               = cap(limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts);
    m_info.max_color_texture_samples                 = cap(limits.framebufferColorSampleCounts);
    m_info.max_depth_texture_samples                 = cap(limits.framebufferDepthSampleCounts);
    m_info.max_framebuffer_samples                   = cap(limits.framebufferColorSampleCounts);
    m_info.max_integer_samples                       = cap(limits.framebufferColorSampleCounts);
    m_info.max_texture_size                          = cap(limits.maxImageDimension2D);
    m_info.max_3d_texture_size                       = cap(limits.maxImageDimension3D);
    m_info.max_cube_map_texture_size                 = cap(limits.maxImageDimensionCube);
    m_info.max_texture_buffer_size                   = cap(limits.maxTexelBufferElements);
    m_info.max_array_texture_layers                  = cap(limits.maxImageArrayLayers);
    m_info.max_texture_max_anisotropy                = limits.maxSamplerAnisotropy;
    m_info.shader_storage_buffer_offset_alignment    = static_cast<unsigned int>(limits.minStorageBufferOffsetAlignment);
    m_info.uniform_buffer_offset_alignment           = static_cast<unsigned int>(limits.minUniformBufferOffsetAlignment);

    // Translate VkPhysicalDeviceDepthStencilResolveProperties into the
    // backend-agnostic Resolve_mode_flag_bit_mask shape that
    // Render_pass uses to validate user-supplied resolve modes.
    auto vk_resolve_modes_to_erhe = [](VkResolveModeFlags vk_modes) -> uint32_t {
        uint32_t out = 0;
        if ((vk_modes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) != 0) out |= Resolve_mode_flag_bit_mask::sample_zero;
        if ((vk_modes & VK_RESOLVE_MODE_AVERAGE_BIT)     != 0) out |= Resolve_mode_flag_bit_mask::average;
        if ((vk_modes & VK_RESOLVE_MODE_MIN_BIT)         != 0) out |= Resolve_mode_flag_bit_mask::min;
        if ((vk_modes & VK_RESOLVE_MODE_MAX_BIT)         != 0) out |= Resolve_mode_flag_bit_mask::max;
        return out;
    };
    m_info.supported_depth_resolve_modes          = vk_resolve_modes_to_erhe(m_depth_stencil_resolve_properties.supportedDepthResolveModes);
    m_info.supported_stencil_resolve_modes        = vk_resolve_modes_to_erhe(m_depth_stencil_resolve_properties.supportedStencilResolveModes);
    m_info.independent_depth_stencil_resolve      = (m_depth_stencil_resolve_properties.independentResolve     == VK_TRUE);
    m_info.independent_depth_stencil_resolve_none = (m_depth_stencil_resolve_properties.independentResolveNone == VK_TRUE);
    // Spec guarantees SAMPLE_ZERO; tolerate buggy/old drivers that report 0.
    if (m_info.supported_depth_resolve_modes == 0) {
        m_info.supported_depth_resolve_modes = Resolve_mode_flag_bit_mask::sample_zero;
    }
    if (m_info.supported_stencil_resolve_modes == 0) {
        m_info.supported_stencil_resolve_modes = Resolve_mode_flag_bit_mask::sample_zero;
    }
    log_context->info(
        "Depth/stencil resolve: depth modes={:#x}, stencil modes={:#x}, independent={}, independent_none={}",
        m_info.supported_depth_resolve_modes,
        m_info.supported_stencil_resolve_modes,
        m_info.independent_depth_stencil_resolve,
        m_info.independent_depth_stencil_resolve_none
    );

    // Create pipeline cache
    {
        const VkPipelineCacheCreateInfo pipeline_cache_create_info{
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .initialDataSize = 0,
            .pInitialData    = nullptr
        };
        result = vkCreatePipelineCache(m_vulkan_device, &pipeline_cache_create_info, nullptr, &m_pipeline_cache);
        if (result != VK_SUCCESS) {
            log_context->error("vkCreatePipelineCache() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        }
    }

    // Create descriptor set layout and pipeline layout.
    // Use a single set (set 0) with bindings for UBOs, SSBOs, and combined image samplers.
    // GLSL layout(binding = N) maps to set = 0, binding = N.
    {
        static constexpr uint32_t max_bindings = 16;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(max_bindings);

        // Bindings 0..7 for uniform buffers
        for (uint32_t i = 0; i < 8; ++i) {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding            = i,
                .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount    = 1,
                .stageFlags         = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = nullptr
            });
        }

        // Bindings 8..11 for storage buffers
        for (uint32_t i = 8; i < 12; ++i) {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding            = i,
                .descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount    = 1,
                .stageFlags         = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = nullptr
            });
        }

        // Bindings 12..15 for combined image samplers
        for (uint32_t i = 12; i < 16; ++i) {
            bindings.push_back(VkDescriptorSetLayoutBinding{
                .binding            = i,
                .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount    = 1,
                .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            });
        }

        VkDescriptorSetLayoutCreateFlags layout_flags = 0;
        if (m_device_extensions.m_VK_KHR_push_descriptor) {
            layout_flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        }

        const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = nullptr,
            .flags        = layout_flags,
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data()
        };

        result = vkCreateDescriptorSetLayout(m_vulkan_device, &descriptor_set_layout_create_info, nullptr, &m_descriptor_set_layout);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateDescriptorSetLayout() failed with {} {}", static_cast<int32_t>(result), c_str(result));
            abort();
        }

        // Create a second descriptor set layout for texture heap (set 1)
        // This uses descriptor indexing for a large array of combined image samplers
        {
            static constexpr uint32_t max_texture_heap_size = 256;
            const VkDescriptorBindingFlags texture_binding_flags =
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

            const VkDescriptorSetLayoutBindingFlagsCreateInfo texture_binding_flags_info{
                .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .pNext         = nullptr,
                .bindingCount  = 1,
                .pBindingFlags = &texture_binding_flags
            };

            const VkDescriptorSetLayoutBinding texture_binding{
                .binding            = 0,
                .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount    = max_texture_heap_size,
                .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = nullptr
            };

            const VkDescriptorSetLayoutCreateInfo texture_layout_create_info{
                .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .pNext        = &texture_binding_flags_info,
                .flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                .bindingCount = 1,
                .pBindings    = &texture_binding
            };

            result = vkCreateDescriptorSetLayout(m_vulkan_device, &texture_layout_create_info, nullptr, &m_texture_set_layout);
            if (result != VK_SUCCESS) {
                log_context->warn("Texture heap descriptor set layout creation failed with {} - textures may not work", static_cast<int32_t>(result));
                m_texture_set_layout = VK_NULL_HANDLE;
            }
        }

        // Create per-frame descriptor pool for fallback binding (when push descriptors unavailable)
        if (!m_device_extensions.m_VK_KHR_push_descriptor) {
            const VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         256},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         256},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256}
            };

            const VkDescriptorPoolCreateInfo pool_create_info{
                .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext         = nullptr,
                .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets       = 256,
                .poolSizeCount = 3,
                .pPoolSizes    = pool_sizes
            };

            result = vkCreateDescriptorPool(m_vulkan_device, &pool_create_info, nullptr, &m_per_frame_descriptor_pool);
            if (result != VK_SUCCESS) {
                log_context->error("vkCreateDescriptorPool() for fallback pool failed with {}", static_cast<int32_t>(result));
            }
        }

        log_context->info(
            "Pipeline infrastructure created: push_descriptors={}",
            m_device_extensions.m_VK_KHR_push_descriptor ? "yes" : "no"
        );
    }

    // GPU timer infrastructure: a single VkQueryPool of
    // s_max_gpu_timers * 2 timestamps per frame in flight. Each
    // Gpu_timer_impl is assigned a slot index (0..s_max_gpu_timers-1)
    // when constructed. Per-slice reset is recorded by the first
    // Command_buffer::begin of each frame (driven by m_gpu_timer_reset_pending,
    // see Command_buffer_impl::begin). Results are read from the slice
    // we are about to reuse in Device_impl::wait_frame, AFTER the
    // timeline-semaphore wait that guarantees the slice has completed.
    {
        // Re-query the graphics queue family's properties to read
        // timestampValidBits. query_device_queue_family_indices already
        // ran during physical-device selection but threw the properties
        // away.
        uint32_t qf_count{0};
        vkGetPhysicalDeviceQueueFamilyProperties(m_vulkan_physical_device, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qf_properties(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(m_vulkan_physical_device, &qf_count, qf_properties.data());

        const uint32_t timestamp_valid_bits =
            (m_graphics_queue_family_index < qf_count)
                ? qf_properties[m_graphics_queue_family_index].timestampValidBits
                : 0u;

        m_gpu_timer_timestamp_period = static_cast<double>(limits.timestampPeriod);
        m_gpu_timers_supported       = (timestamp_valid_bits > 0) && (m_gpu_timer_timestamp_period > 0.0);
        m_gpu_timer_valid_mask       = (timestamp_valid_bits >= 64u)
            ? ~static_cast<uint64_t>(0)
            : ((static_cast<uint64_t>(1) << timestamp_valid_bits) - 1u);

        if (!m_gpu_timers_supported) {
            log_startup->info(
                "GPU timers disabled: timestampValidBits={} timestampPeriod={}",
                timestamp_valid_bits, m_gpu_timer_timestamp_period
            );
        } else {
            const uint32_t total_queries = static_cast<uint32_t>(
                s_max_gpu_timers * 2u * s_number_of_frames_in_flight
            );
            const VkQueryPoolCreateInfo query_pool_create_info{
                .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .pNext              = nullptr,
                .flags              = 0,
                .queryType          = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount         = total_queries,
                .pipelineStatistics = 0
            };
            const VkResult qp_result = vkCreateQueryPool(
                m_vulkan_device, &query_pool_create_info, nullptr, &m_gpu_timer_query_pool
            );
            if (qp_result != VK_SUCCESS) {
                log_context->warn(
                    "vkCreateQueryPool() for GPU timers failed with {} {}; GPU timers disabled",
                    static_cast<int32_t>(qp_result), c_str(qp_result)
                );
                m_gpu_timer_query_pool = VK_NULL_HANDLE;
                m_gpu_timers_supported = false;
            } else {
                set_debug_label(
                    VK_OBJECT_TYPE_QUERY_POOL,
                    reinterpret_cast<uint64_t>(m_gpu_timer_query_pool),
                    "GPU timer query pool"
                );
                // Vulkan requires queries to be reset before first use.
                // We don't have a CB here, so prime the per-slice reset
                // flag for every frame slot; the first cb of each frame
                // will record vkCmdResetQueryPool for its slice before
                // any timer fires.
                for (bool& pending : m_gpu_timer_reset_pending) {
                    pending = true;
                }
                log_startup->info(
                    "GPU timers enabled: timestampValidBits={} timestampPeriod={} max_timers={}",
                    timestamp_valid_bits, m_gpu_timer_timestamp_period, s_max_gpu_timers
                );
                // Frame-spanning GPU timestamp bracket ring (step P0.3):
                // needs host query reset so wait_frame can recycle pairs
                // without a command buffer.
                if (m_host_query_reset) {
                    const VkQueryPoolCreateInfo bracket_pool_create_info{
                        .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                        .pNext              = nullptr,
                        .flags              = 0,
                        .queryType          = VK_QUERY_TYPE_TIMESTAMP,
                        .queryCount         = static_cast<uint32_t>(s_frame_bracket_ring * 2u),
                        .pipelineStatistics = 0
                    };
                    const VkResult bracket_result = vkCreateQueryPool(
                        m_vulkan_device, &bracket_pool_create_info, nullptr, &m_frame_bracket_query_pool
                    );
                    if (bracket_result != VK_SUCCESS) {
                        log_context->warn(
                            "vkCreateQueryPool() for the frame bracket failed with {} {}; frame GPU spans disabled",
                            static_cast<int32_t>(bracket_result), c_str(bracket_result)
                        );
                        m_frame_bracket_query_pool = VK_NULL_HANDLE;
                    } else {
                        set_debug_label(
                            VK_OBJECT_TYPE_QUERY_POOL,
                            reinterpret_cast<uint64_t>(m_frame_bracket_query_pool),
                            "Frame bracket query pool"
                        );
                        vkResetQueryPool(
                            m_vulkan_device,
                            m_frame_bracket_query_pool,
                            0,
                            static_cast<uint32_t>(s_frame_bracket_ring * 2u)
                        );
                        m_frame_bracket_frame_id.fill(-1);
                        log_startup->info("Frame GPU timestamp bracket enabled (ring of {})", s_frame_bracket_ring);
                    }
                } else {
                    log_startup->info("Frame GPU timestamp bracket disabled: hostQueryReset not supported");
                }
#if defined(ERHE_PROFILE_LIBRARY_TRACY) && defined(TRACY_ENABLE)
                // Tracy GPU (Vulkan) profiling context. Tracy records a
                // calibration into a one-shot command buffer and submits +
                // waits on it internally, so a throwaway transient pool is
                // enough. Created here, where timestamp support is confirmed;
                // the per-frame TracyVkCollect and the shutdown TracyVkDestroy
                // live in vulkan_device.cpp.
                if (m_vulkan_graphics_queue != VK_NULL_HANDLE) {
                    // Tracy's VkCtx constructor begins/submits/waits the cb
                    // MORE than once (its calibration sequence); the second
                    // vkBeginCommandBuffer is an implicit reset, which
                    // requires the pool's RESET_COMMAND_BUFFER bit
                    // (VUID-vkBeginCommandBuffer-commandBuffer-00050).
                    const VkCommandPoolCreateInfo tracy_pool_create_info{
                        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                        .pNext            = nullptr,
                        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                        .queueFamilyIndex = m_graphics_queue_family_index
                    };
                    VkCommandPool tracy_command_pool = VK_NULL_HANDLE;
                    if (vkCreateCommandPool(m_vulkan_device, &tracy_pool_create_info, nullptr, &tracy_command_pool) == VK_SUCCESS) {
                        const VkCommandBufferAllocateInfo tracy_cb_allocate_info{
                            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                            .pNext              = nullptr,
                            .commandPool        = tracy_command_pool,
                            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                            .commandBufferCount = 1
                        };
                        VkCommandBuffer tracy_command_buffer = VK_NULL_HANDLE;
                        if (vkAllocateCommandBuffers(m_vulkan_device, &tracy_cb_allocate_info, &tracy_command_buffer) == VK_SUCCESS) {
                            m_tracy_vk_ctx = TracyVkContext(
                                m_vulkan_physical_device, m_vulkan_device, m_vulkan_graphics_queue, tracy_command_buffer
                            );
                            static constexpr char tracy_ctx_name[] = "erhe graphics queue";
                            TracyVkContextName(m_tracy_vk_ctx, tracy_ctx_name, static_cast<uint16_t>(sizeof(tracy_ctx_name) - 1));
                            log_startup->info("Tracy Vulkan GPU profiling context created");
                        }
                        // TracyVkContext submitted + waited on tracy_command_buffer
                        // internally, so the transient pool can be freed now.
                        vkDestroyCommandPool(m_vulkan_device, tracy_command_pool, nullptr);
                    }
                }
#endif
            }
        }
    }
}

}
