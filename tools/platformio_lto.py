"""Keep S2 firmware within the existing OTA slots without removing features."""
Import("env")
env.Append(LINKFLAGS=["-flto"])
