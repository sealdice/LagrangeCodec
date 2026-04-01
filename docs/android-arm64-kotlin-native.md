# Android arm64 Kotlin/Native static bundle

This bundle is produced specifically for `androidNativeArm64` Kotlin/Native executables.

## What is inside

- `lib/libLagrangeCodec.a`
- `lib/libavcodec.a`
- `lib/libavformat.a`
- `lib/libavutil.a`
- `lib/libswresample.a`
- `lib/libswscale.a`
- `lib/libz.a`
- `include/public/*.h`

## Why this bundle differs from a normal Android static build

- LagrangeCodec no longer emits logs through `stderr` / `stdout` on Android; it uses `liblog` instead.
- Android `arm64` dependencies are built directly with the Android NDK toolchain instead of using `vcpkg` prebuilt archives.
- FFmpeg and zlib are compiled with `_FORTIFY_SOURCE` disabled for this static-link scenario, which avoids host/glibc-style fortified unresolved symbols such as `__write_chk` when the archives are linked straight into a Kotlin/Native executable.
- The FFmpeg build is constrained to the minimal codec/container set needed by LagrangeCodec, which reduces accidental dependency bleed from host libraries.

## Replacement target in acidify-codec

Copy every archive under `lib/` into:

`acidify-codec/src/nativeInterop/lib/androidArm64/`

Recommended linker opts for `androidNativeArm64` stay:

- `-llog`
- `-ldl`
- `-latomic`
- `-landroid`
- `-lm`

Recommended static library order:

1. `libLagrangeCodec.a`
2. `libavformat.a`
3. `libavcodec.a`
4. `libswresample.a`
5. `libswscale.a`
6. `libavutil.a`
7. `libz.a`
