# FrameRoom GameOS — R0.13 foundation (NOT a finished Android guest)

## Product decision

Replace the Android managed/work-profile clone as the **primary** game launch target with
a custom Android userspace running inside the FrameRoom app. The pre-existing R0.12
work profile remains on the phone until its owner explicitly removes it in Android settings;
the GameOS entry point neither creates nor requires another profile.

The goal is a **single-game** Android guest: boot only required Android services,
install the selected game's APK and split APKs, run one immersive game activity,
capture guest frames at its owned compositor, interpolate and present without
MediaProjection. Do not imply that Android restrictions or host sandbox have disappeared.
The host kernel, device SELinux, permissions and hardware capabilities still apply.

## Source base to investigate (no code vendored yet)

- Wayondroid: https://github.com/yoro1836/wayondroid
- Its rootless runtime uses tawcroot, a userspace binder, a Wayland compositor,
  guest LineageOS/Waydroid images, and experimental gfxstream acceleration.
- Status: alpha, and POCO X6 Android 16 remains **untested**. Some integration
  requires review of license obligations (Wayondroid GPL-3.0).
- Waydroid's Android 13 ARM64 system and vendor images are packaged as a `.vrom`.
  A complete ROM can consume several gigabytes of storage and significant RAM.
- Android AVF/pKVM does not offer ordinary third-party apps a general unrestricted
  guest VM API, so the rootless userspace approach must be evaluated on-device.

## What is implemented on this branch

1. `GameOsRomStore`: import ROM archives via Android Storage Access Framework
   into a dedicated private directory, bounded to 8 GiB; check ARM64, required
   manifest/system/vendor entries; calculate SHA-256 before atomic replacement.
2. `GameOsActivity`: independent GameOS screen, ROM import and validation status;
   optional selected-package information, no Android managed-profile provisioning.
3. The personal profile's Games/GameOS entry points target the new screen.
4. The existing Arena/Vulkan demonstration remains available. It is NOT guest
   Android compatibility or universal FG.

## Execution phases (not implemented in the first R0.13 commit)

A. **Boot runtime** — legally import and build the required rootless runtime,
   userspace binder, Android process launcher and framebuffer compositor in
   FrameRoom's native code. Verify AOSP guest boot and stop/restart on POCO X6.
   Do not package a multi-gigabyte ROM into the small APK.
B. **Game-only guest** — use package manager inside the guest to install base and
   split APKs; persist isolated guest data; launch only the selected game,
   remap input/audio and virtual storage; support safe cleanup.
C. **GPU bridge** — guest Vulkan/GLES through gfxstream into FrameRoom's
   compositor. Preserve frame timestamp, original order, acquisition/release
   fences, pixel format and color space for AHardwareBuffer. If the guest
   presents without a valid acquire fence, do not advertise tear-free FG.
D. **Frame Generation** — feed two actual consecutive guest frames to the
   Android-native interpolation implementation, not an unmodified Windows DLL.
   Use native driver synchronization and a deadline-based scheduler. Emit only
   verified new frames; on error, miss, thermal throttling or overrun, fall
   back to presenting originals without stalling input.
E. **Game support** — probe Vulkan/GLES GPU and guest capabilities and test
   specific Android titles. Do not promise compatibility with all apps or
   anti-cheat / DRM-dependent games without evidence.

The intended buffer path:

```
Guest game (Vulkan/GLES)
     -> guest SurfaceFlinger/HWC
     -> guest host GPU transport (fenced AHB)
     -> FrameRoom-owned compositor:
         -> original-frame timeline
         -> motion interpolation on the GPU
         -> display scheduler -> host screen
```

## Mandatory acceptance gates

- Boot Android guest without root, without work profiles, and show a real guest
  app session (not the host's launcher).
- Install a genuine base+split APK set in the guest; run, suspend and resume
  while private data persists separately from the host installation.
- Touch, controller input, audio and network smoke tests.
- Vulkan/GL presentation from the guest; host logs show actual AHB producer IDs
  and acquisition/release synchronization.
- Real frame test: reject blank, duplicate and late frames; distinguish
  original, generated and displayed FPS. Latency must be measured.
- Thermal guard and graceful return to original frames if FG cannot keep up.
- Validate real-device performance on POCO X6 5G/Adreno 710/Android 16.
- CI compile success alone is **not** evidence of successful guest boot or FG.

## Safety / storage

ROMs are not trusted code merely because a ZIP manifest parses. Before
extracting/booting, verify source, checksums, image signatures where possible,
path traversal protection, ext4 bounds and ROM compatibility. Android sandbox
restrictions still apply to the host app. A rootless Android guest sharing the
host app UID is **not** a strong security boundary between guest apps.

Do not silently migrate or remove the user's existing managed profile.
