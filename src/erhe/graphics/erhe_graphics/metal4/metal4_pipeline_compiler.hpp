#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace MTL  { class ComputePipelineState; }
namespace MTL  { class Device; }
namespace MTL  { class Library; }
namespace MTL  { class RenderPipelineState; }
namespace MTL4 { class Archive; }
namespace MTL4 { class Compiler; }
namespace MTL4 { class CompilerTaskOptions; }
namespace MTL4 { class ComputePipelineDescriptor; }
namespace MTL4 { class LibraryFunctionDescriptor; }
namespace MTL4 { class PipelineDataSetSerializer; }
namespace MTL4 { class RenderPipelineDescriptor; }

namespace erhe::graphics {

// The device's MTL4::Compiler (doc/erhe/metal4_backend.md "Shaders and
// pipelines"). Every library and pipeline state of the backend is created
// here. Pipelines are compiled with the archive loaded at creation as the
// lookup archive, and the compiler's pipeline data set serializer captures
// every pipeline it creates (archive hits included), so write_archive()
// persists the binaries of all pipelines this run created
// (metal4_pipeline_archive_file.hpp: file name, manifest).
//
// Thread safe: MTL4::Compiler and MTL4::Archive are thread-safe protocols
// (NS_SWIFT_SENDABLE in the Metal headers), m_options is written only in
// the constructor and only read after it, and the statistics are under a
// mutex. write_archive() requires that every thread creating pipelines has
// stopped (the device destructor calls it).
//
// The callers create each distinct pipeline once: Render_pipeline and
// Compute_pipeline at construction, the encoders' per-bind paths through
// Pipeline_state_cache (metal4_pipeline_state_cache.hpp), so the creation
// count of the summary line counts distinct pipelines.
class Pipeline_compiler final
{
public:
    explicit Pipeline_compiler(MTL::Device* device);
    ~Pipeline_compiler() noexcept;
    Pipeline_compiler (const Pipeline_compiler&) = delete;
    void operator=    (const Pipeline_compiler&) = delete;

    // MSL source -> library. Returns nullptr and sets out_error on failure.
    [[nodiscard]] auto new_library(
        std::string_view msl_source,
        std::string_view label,
        std::string&     out_error
    ) -> MTL::Library*;

    // Retained descriptor naming a function of the library; nullptr when the
    // library has no function of that name.
    [[nodiscard]] auto new_function_descriptor(
        MTL::Library*    library,
        std::string_view function_name
    ) -> MTL4::LibraryFunctionDescriptor*;

    [[nodiscard]] auto new_render_pipeline(
        const MTL4::RenderPipelineDescriptor* descriptor,
        std::string&                          out_error
    ) -> MTL::RenderPipelineState*;

    [[nodiscard]] auto new_compute_pipeline(
        const MTL4::ComputePipelineDescriptor* descriptor,
        std::string&                           out_error
    ) -> MTL::ComputePipelineState*;

    // Raw MSL compute kernel (backend-internal kernels) -> pipeline state.
    [[nodiscard]] auto new_compute_pipeline_from_source(
        std::string_view msl_source,
        std::string_view function_name,
        std::string_view label,
        std::string&     out_error
    ) -> MTL::ComputePipelineState*;

    // Logs the startup summary line once: pipelines created, how many of
    // them the archive held, compile time. Counting archive hits probes the
    // archive before each compile, which only the startup window pays for:
    // after this call pipelines are compiled without the probe.
    void log_startup_summary();

    // Serializes the captured pipelines to the archive file (atomically:
    // temporary file, then rename). Called once, at device destruction.
    void write_archive();

private:
    enum class Pipeline_source : unsigned int { compiled, archive };
    void note_pipeline(Pipeline_source source, double milliseconds);

    MTL::Device*                     m_device    {nullptr};
    MTL4::PipelineDataSetSerializer* m_serializer{nullptr};
    MTL4::Compiler*                  m_compiler  {nullptr};
    MTL4::Archive*                   m_archive   {nullptr};
    MTL4::CompilerTaskOptions*       m_options   {nullptr};
    std::filesystem::path            m_archive_path;

    std::mutex                       m_statistics_mutex;
    bool                             m_startup_window      {true};
    uint64_t                         m_pipeline_count      {0};
    uint64_t                         m_archive_hit_count   {0};
    uint64_t                         m_library_count       {0};
    double                           m_pipeline_time_ms    {0.0};
    double                           m_library_time_ms     {0.0};
};

} // namespace erhe::graphics
