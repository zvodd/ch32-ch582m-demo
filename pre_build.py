import os
Import("env")

# This ensures clangd finds all necessary toolchain/core library headers
env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)
