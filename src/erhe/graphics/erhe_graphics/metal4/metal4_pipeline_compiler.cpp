#include "erhe_graphics/metal4/metal4_pipeline_compiler.hpp"
#include "erhe_graphics/metal4/metal4_pipeline_archive_file.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_verify/verify.hpp"

#include <Metal/Metal.hpp>

#include <fmt/format.h>

#include <sys/sysctl.h>
#include <unistd.h>

#include <chrono>
#include <system_error>

namespace erhe::graphics {

namespace {

// kern.osversion, the OS build ("25A354"); "unknown" when unavailable.
auto get_os_build() -> std::string
{
    char        buffer[64]{};
    std::size_t size = sizeof(buffer);
    if ((sysctlbyname("kern.osversion", buffer, &size, nullptr, 0) != 0) || (size == 0)) {
        return std::string{"unknown"};
    }
    return std::string{buffer};
}

auto make_ns_string(const std::string_view text) -> NS::String*
{
    // Autoreleased; callers hold a Scoped_transient_object_pool.
    const std::string copy{text};
    return NS::String::string(copy.c_str(), NS::UTF8StringEncoding);
}

auto error_string(NS::Error* error) -> std::string
{
    if ((error == nullptr) || (error->localizedDescription() == nullptr)) {
        return std::string{"(no error)"};
    }
    return std::string{error->localizedDescription()->utf8String()};
}

auto elapsed_ms(const std::chrono::steady_clock::time_point begin) -> double
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

} // anonymous namespace

Pipeline_compiler::Pipeline_compiler(MTL::Device* const device)
    : m_device{device}
{
    ERHE_VERIFY(m_device != nullptr);
    Scoped_transient_object_pool object_pool{};

    m_archive_path = make_metal4_pipeline_archive_path(
        std::filesystem::path{"pipeline_cache"},
        std::string_view{m_device->name()->utf8String()},
        get_os_build()
    );

    MTL4::PipelineDataSetSerializerDescriptor* const serializer_descriptor = MTL4::PipelineDataSetSerializerDescriptor::alloc()->init();
    serializer_descriptor->setConfiguration(MTL4::PipelineDataSetSerializerConfigurationCaptureBinaries);
    m_serializer = m_device->newPipelineDataSetSerializer(serializer_descriptor);
    serializer_descriptor->release();
    if (m_serializer == nullptr) {
        log_startup->warn("Metal 4: newPipelineDataSetSerializer failed; the pipeline archive is not written");
    }

    MTL4::CompilerDescriptor* const compiler_descriptor = MTL4::CompilerDescriptor::alloc()->init();
    compiler_descriptor->setLabel(NS::String::string("erhe", NS::UTF8StringEncoding));
    if (m_serializer != nullptr) {
        compiler_descriptor->setPipelineDataSetSerializer(m_serializer);
    }
    NS::Error* error = nullptr;
    m_compiler = m_device->newCompiler(compiler_descriptor, &error);
    compiler_descriptor->release();
    if (m_compiler == nullptr) {
        log_startup->critical("Metal 4: newCompiler failed: {}", error_string(error));
        abort();
    }

    m_options = MTL4::CompilerTaskOptions::alloc()->init();

    std::error_code ec;
    std::string     reason;
    if (!std::filesystem::exists(m_archive_path, ec)) {
        log_startup->info("Metal 4 pipeline archive {}: none, pipelines are compiled", m_archive_path.string());
    } else if (!check_metal4_pipeline_archive(m_archive_path, reason)) {
        // newArchive crashes on a file that is not an archive: only an
        // archive its manifest vouches for is opened. Everything compiles,
        // and the file is replaced at device destruction.
        log_startup->warn("Metal 4 pipeline archive {} ignored ({}); it is rewritten at exit", m_archive_path.string(), reason);
    } else {
        NS::URL* const url = NS::URL::fileURLWithPath(make_ns_string(m_archive_path.string()));
        error = nullptr;
        m_archive = m_device->newArchive(url, &error);
        if (m_archive == nullptr) {
            log_startup->warn(
                "Metal 4 pipeline archive {} ignored (not readable: {}); it is rewritten at exit",
                m_archive_path.string(), error_string(error)
            );
        } else {
            const std::uintmax_t size = std::filesystem::file_size(m_archive_path, ec);
            log_startup->info("Metal 4 pipeline archive {}: {} bytes loaded", m_archive_path.string(), ec ? 0 : size);
            const MTL4::Archive* const archives[1]{m_archive};
            m_options->setLookupArchives(NS::Array::array(reinterpret_cast<const NS::Object* const*>(archives), 1));
        }
    }
}

Pipeline_compiler::~Pipeline_compiler() noexcept
{
    if (m_options != nullptr) {
        m_options->release();
        m_options = nullptr;
    }
    if (m_archive != nullptr) {
        m_archive->release();
        m_archive = nullptr;
    }
    if (m_compiler != nullptr) {
        m_compiler->release();
        m_compiler = nullptr;
    }
    if (m_serializer != nullptr) {
        m_serializer->release();
        m_serializer = nullptr;
    }
}

auto Pipeline_compiler::new_library(
    const std::string_view msl_source,
    const std::string_view label,
    std::string&           out_error
) -> MTL::Library*
{
    Scoped_transient_object_pool object_pool{};
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    MTL4::LibraryDescriptor* const descriptor = MTL4::LibraryDescriptor::alloc()->init();
    MTL::CompileOptions*     const options    = MTL::CompileOptions::alloc()->init();
    descriptor->setSource (make_ns_string(msl_source));
    descriptor->setName   (make_ns_string(label));
    descriptor->setOptions(options);
    NS::Error* error = nullptr;
    MTL::Library* const library = m_compiler->newLibrary(descriptor, &error);
    options->release();
    descriptor->release();
    if (library == nullptr) {
        out_error = error_string(error);
        return nullptr;
    }
    const double milliseconds = elapsed_ms(begin);
    const std::lock_guard<std::mutex> lock{m_statistics_mutex};
    ++m_library_count;
    m_library_time_ms += milliseconds;
    return library;
}

auto Pipeline_compiler::new_function_descriptor(
    MTL::Library* const    library,
    const std::string_view function_name
) -> MTL4::LibraryFunctionDescriptor*
{
    ERHE_VERIFY(library != nullptr);
    Scoped_transient_object_pool object_pool{};
    NS::String* const name           = make_ns_string(function_name);
    NS::Array*  const function_names = library->functionNames();
    bool found = false;
    for (NS::UInteger i = 0, end = function_names->count(); i < end; ++i) {
        if (static_cast<NS::String*>(function_names->object(i))->isEqualToString(name)) {
            found = true;
            break;
        }
    }
    if (!found) {
        return nullptr;
    }
    MTL4::LibraryFunctionDescriptor* const descriptor = MTL4::LibraryFunctionDescriptor::alloc()->init();
    descriptor->setLibrary(library);
    descriptor->setName(name);
    return descriptor;
}

void Pipeline_compiler::note_pipeline(const Pipeline_source source, const double milliseconds)
{
    const std::lock_guard<std::mutex> lock{m_statistics_mutex};
    ++m_pipeline_count;
    if (source == Pipeline_source::archive) {
        ++m_archive_hit_count;
    }
    m_pipeline_time_ms += milliseconds;
}

auto Pipeline_compiler::new_render_pipeline(
    const MTL4::RenderPipelineDescriptor* const descriptor,
    std::string&                                out_error
) -> MTL::RenderPipelineState*
{
    Scoped_transient_object_pool object_pool{};
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    bool probe = false;
    {
        const std::lock_guard<std::mutex> lock{m_statistics_mutex};
        probe = m_startup_window && (m_archive != nullptr);
    }
    Pipeline_source source = Pipeline_source::compiled;
    if (probe) {
        // The archive alone answers whether it holds the pipeline; the
        // compiler below serves it from the same archive (lookupArchives) and
        // so captures it for the next archive.
        NS::Error* probe_error = nullptr;
        MTL::RenderPipelineState* const probed = m_archive->newRenderPipelineState(descriptor, &probe_error);
        if (probed != nullptr) {
            source = Pipeline_source::archive;
            probed->release();
        }
    }
    NS::Error* error = nullptr;
    MTL::RenderPipelineState* const pipeline = m_compiler->newRenderPipelineState(descriptor, m_options, &error);
    if (pipeline == nullptr) {
        out_error = error_string(error);
        return nullptr;
    }
    note_pipeline(source, elapsed_ms(begin));
    return pipeline;
}

auto Pipeline_compiler::new_compute_pipeline(
    const MTL4::ComputePipelineDescriptor* const descriptor,
    std::string&                                 out_error
) -> MTL::ComputePipelineState*
{
    Scoped_transient_object_pool object_pool{};
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    bool probe = false;
    {
        const std::lock_guard<std::mutex> lock{m_statistics_mutex};
        probe = m_startup_window && (m_archive != nullptr);
    }
    Pipeline_source source = Pipeline_source::compiled;
    if (probe) {
        NS::Error* probe_error = nullptr;
        MTL::ComputePipelineState* const probed = m_archive->newComputePipelineState(descriptor, &probe_error);
        if (probed != nullptr) {
            source = Pipeline_source::archive;
            probed->release();
        }
    }
    NS::Error* error = nullptr;
    MTL::ComputePipelineState* const pipeline = m_compiler->newComputePipelineState(descriptor, m_options, &error);
    if (pipeline == nullptr) {
        out_error = error_string(error);
        return nullptr;
    }
    note_pipeline(source, elapsed_ms(begin));
    return pipeline;
}

auto Pipeline_compiler::new_compute_pipeline_from_source(
    const std::string_view msl_source,
    const std::string_view function_name,
    const std::string_view label,
    std::string&           out_error
) -> MTL::ComputePipelineState*
{
    Scoped_transient_object_pool object_pool{};
    MTL::Library* const library = new_library(msl_source, label, out_error);
    if (library == nullptr) {
        return nullptr;
    }
    MTL4::LibraryFunctionDescriptor* const function = new_function_descriptor(library, function_name);
    library->release();
    if (function == nullptr) {
        out_error = fmt::format("function '{}' not found", function_name);
        return nullptr;
    }
    MTL4::ComputePipelineDescriptor* const descriptor = MTL4::ComputePipelineDescriptor::alloc()->init();
    descriptor->setComputeFunctionDescriptor(function);
    descriptor->setLabel(make_ns_string(label));
    MTL::ComputePipelineState* const pipeline = new_compute_pipeline(descriptor, out_error);
    descriptor->release();
    function->release();
    return pipeline;
}

void Pipeline_compiler::log_startup_summary()
{
    const std::lock_guard<std::mutex> lock{m_statistics_mutex};
    if (!m_startup_window) {
        return;
    }
    m_startup_window = false;
    log_startup->info(
        "Metal 4 pipelines at startup: {} created, {} from archive, {:.1f} ms pipeline compile; {} libraries, {:.1f} ms library compile",
        m_pipeline_count, m_archive_hit_count, m_pipeline_time_ms, m_library_count, m_library_time_ms
    );
}

void Pipeline_compiler::write_archive()
{
    if (m_serializer == nullptr) {
        return;
    }
    Scoped_transient_object_pool object_pool{};
    const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    std::error_code ec;
    std::filesystem::create_directories(m_archive_path.parent_path(), ec);
    if (ec) {
        log_context->warn("Metal 4 pipeline archive {} not written: {}", m_archive_path.string(), ec.message());
        return;
    }
    const std::size_t stale_count = remove_stale_metal4_pipeline_archive_temporaries(m_archive_path);
    if (stale_count > 0) {
        log_context->info("Metal 4 pipeline archive: removed {} temporary files of interrupted writes", stale_count);
    }
    std::filesystem::path temporary_path = m_archive_path;
    temporary_path += fmt::format(".{}.tmp", static_cast<long>(getpid()));
    NS::URL* const url = NS::URL::fileURLWithPath(make_ns_string(temporary_path.string()));
    NS::Error* error = nullptr;
    if (!m_serializer->serializeAsArchiveAndFlushToURL(url, &error)) {
        // "Nothing to serialize" when the run created no pipeline.
        log_context->warn("Metal 4 pipeline archive {} not written: {}", m_archive_path.string(), error_string(error));
        std::filesystem::remove(temporary_path, ec);
        return;
    }
    std::string install_error;
    if (!install_metal4_pipeline_archive(temporary_path, m_archive_path, install_error)) {
        log_context->warn("Metal 4 pipeline archive {} not written: {}", m_archive_path.string(), install_error);
        return;
    }
    const std::uintmax_t size = std::filesystem::file_size(m_archive_path, ec);
    uint64_t pipeline_count = 0;
    {
        const std::lock_guard<std::mutex> lock{m_statistics_mutex};
        pipeline_count = m_pipeline_count;
    }
    log_context->info(
        "Metal 4 pipeline archive {}: wrote {} bytes ({} pipeline creations this run) in {:.1f} ms",
        m_archive_path.string(), ec ? 0 : size, pipeline_count, elapsed_ms(begin)
    );
}

} // namespace erhe::graphics
