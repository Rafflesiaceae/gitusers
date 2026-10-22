cJSON 1.7.19, vendored without modifications from https://github.com/DaveGamble/cJSON/tree/v1.7.19. See LICENSE in this directory.

Meson compiles these sources with the application's compiler so a musl build never links to a host libc's cJSON library. Unused cJSON functions are removed at link time.
