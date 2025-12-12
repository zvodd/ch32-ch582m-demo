from os.path import join
Import("env")



raw_command = " ".join([
    "$OBJCOPY",
    "-O", "binary",
    "$BUILD_DIR/firmware.elf",
    "$BUILD_DIR/firmware.bin"
])

expanded_command = env.subst(raw_command)

# Add a post-action to convert ELF to BIN and rename
env.AddPostAction(
    "$BUILD_DIR/firmware.elf",
    env.VerboseAction(
        raw_command,
        f"Converting to firmware.bin | `{expanded_command}`"  # Fixed the message to match the output file
    )
)