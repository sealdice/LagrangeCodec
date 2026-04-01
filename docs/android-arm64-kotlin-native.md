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
- FFmpeg Android static builds are sanitized to drop `_FORTIFY_SOURCE`, which avoids host/glibc-style fortified unresolved symbols such as `__write_chk` when the archives are linked straight into a Kotlin/Native executable.
- Android dependency lookup is restricted to the target sysroot / vcpkg target libraries during the build, which reduces the risk of accidentally pulling host libraries into the arm64 archive set.

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
