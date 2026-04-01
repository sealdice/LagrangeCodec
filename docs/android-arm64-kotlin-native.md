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

`lib/libLagrangeCodec.a` is the preferred Android artifact. In the Android bundle it is a merged static archive that already contains the LagrangeCodec objects plus FFmpeg/zlib objects needed by Kotlin/Native final linking.

## Why this bundle differs from a normal Android static build

- LagrangeCodec no longer emits logs through `stderr` / `stdout` on Android; it uses `liblog` instead.
- Android `arm64` dependencies are built directly with the Android NDK toolchain instead of using `vcpkg` prebuilt archives.
- FFmpeg and zlib are compiled with `_FORTIFY_SOURCE` disabled for this static-link scenario, which avoids host/glibc-style fortified unresolved symbols such as `__write_chk` when the archives are linked straight into a Kotlin/Native executable.
- The FFmpeg build is constrained to the minimal codec/container set needed by LagrangeCodec, which reduces accidental dependency bleed from host libraries.

## Replacement target in acidify-codec

Copy the bundle into:

`acidify-codec/src/nativeInterop/lib/androidArm64/`

For Kotlin/Native Android, prefer linking `libLagrangeCodec.a` first. The separate `libav*.a` and `libz.a` files are kept in the bundle mainly for debugging and fallback use.

Recommended linker opts for `androidNativeArm64` stay:

- `-llog`
- `-ldl`
- `-latomic`
- `-landroid`
- `-lm`

Recommended static library order:

1. `libLagrangeCodec.a`
