"""Keep firmware compact on boards with 4 MB Flash."""
Import("env")
env.Append(LINKFLAGS=["-flto", "-flto="+str(max(1, int(env.GetOption("num_jobs"))))])
