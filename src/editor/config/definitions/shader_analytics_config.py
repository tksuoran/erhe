from erhe_codegen import *

# doc/editor/shader_analytics.md
struct("Shader_analytics_config",
    reflect=True,
    version=1,
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
            long_desc="Analyze every program the editor builds with the Mali offline compiler in background tasks. Read at startup; never on Android; --no-shader-analytics turns it off.",
            visible=True,
            developer=False
        ),
        field(
            "malioc",
            String,
            added_in=1,
            default='""',
            short_desc="malioc Path",
            long_desc="Arm Mali Offline Compiler executable. Empty: the MALIOC environment variable, then PATH.",
            visible=True,
            developer=False
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
