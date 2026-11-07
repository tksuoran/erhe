from erhe_codegen import *

# doc/editor/shader_analytics.md
struct("Shader_analytics_config",
    reflect=True,
    version=3,
    short_desc="Shader Analytics",
    long_desc="Offline compiler analysis of every shader program the editor builds, for the shader cost overdraw modes.",
    developer=False,
    fields=[
        field(
            "enabled",
            Bool,
            added_in=1,
            default="true",
            short_desc="Enabled",
            long_desc="Analyze every program the editor builds with the Mali and PowerVR offline compilers in background tasks. Read at startup; never on Android; --no-shader-analytics turns it off.",
            visible=True,
            developer=False
        ),
        # malioc (v1, one executable path) was replaced by the search path
        # list in v2; an older file's key is ignored on load.
        field(
            "malioc_search_paths",
            Vector(String),
            added_in=2,
            short_desc="malioc Search Paths",
            long_desc="Arm Mali Offline Compiler executables, or directories holding malioc, searched with the ERHE_MALIOC path list and PATH; $NAME, ${NAME} and a leading ~ expand. The newest version found is used.",
        ),
        field(
            "powervr_search_paths",
            Vector(String),
            added_in=3,
            short_desc="PowerVR Search Paths",
            long_desc="Imagination PowerVR profiling compiler executables (ProfilingCompilerCLIWrapper, its compiler libraries beside it), or directories holding it, searched with the ERHE_POWERVR path list and PATH; $NAME, ${NAME} and a leading ~ expand. The newest compiler library build found is used.",
        ),
        field(
            "mali_core",
            String,
            added_in=1,
            default='"Mali-G310"',
            short_desc="Mali Core",
            long_desc="The Mali core malioc compiles for (malioc --list names them).",
            visible=True,
            developer=False
        ),
        field(
            "powervr_core",
            String,
            added_in=3,
            default='"PowerVR-BXM-8-256"',
            short_desc="PowerVR Core",
            long_desc="The PowerVR core the profiling compiler compiles for, PowerVR-<GPU> or PowerVR-<BVNC> (ProfilingCompilerCLIWrapper -h lists them; the SDK must hold that BVNC's SPIR-V compiler library).",
            visible=True,
            developer=False
        ),
        field(
            "max_jobs",
            Int,
            added_in=1,
            default="2",
            short_desc="Parallel Jobs",
            long_desc="Background tasks running compilers at once; each holds one worker thread while its compiler runs.",
            visible=True,
            developer=False
        ),
    ]
)
