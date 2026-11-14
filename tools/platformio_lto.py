"""Keep firmware compact on boards with 4 MB Flash."""
Import("env")
env.Append(LINKFLAGS=["-flto", "-flto="+str(max(1, int(env.GetOption("num_jobs"))))])

# The small-flash S2 profile has no C++ exception recovery. Ruckig is used
# through its error-returning interface; keep all runtime safety checks while
# omitting exception landing pads/unwind tables from application/Arduino code.
flags = env.ParseFlagsExtended(env.GetProjectOption('build_flags', []))['CPPDEFINES']
if any(isinstance(flag, (tuple, list)) and flag[0] == 'G5500_COMPACT_DECIMAL' and str(flag[1]) == '1' for flag in flags):
    env.Append(LINKFLAGS=['-Wl,-Map='+env.subst('$BUILD_DIR/firmware.map'),
                          '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables'])
