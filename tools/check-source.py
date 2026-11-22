#!/usr/bin/env python3
"""Static wiring checks; not evidence that Vulkan GPU calls work."""
from pathlib import Path
r = Path(__file__).resolve().parents[1]
cpp = (r/'app/src/main/cpp/vulkan_demo.cpp').read_text()
ahb = (r/'app/src/main/cpp/ahb_offscreen.cpp').read_text()
java = (r/'app/src/main/java/dev/frameroom/VulkanPlayerView.java').read_text()
fg = (r/'app/src/main/cpp/framegen_vulkan.cpp').read_text()
cmake = (r/'app/src/main/cpp/CMakeLists.txt').read_text()
gradle = (r/'app/build.gradle.kts').read_text()
workflow = (r/'.github/workflows/android-debug.yml').read_text()
space = (r/'app/src/main/java/dev/frameroom/SpaceInstallActivity.java').read_text()
profile = (r/'app/src/main/java/dev/frameroom/SpaceProfile.java').read_text()
checks = {
  'Player JNI surface/session binding': 'nativeStart(Surface surface, int fps, boolean ahb, boolean live,' in java and
      'VulkanPlayerView_nativeStart' in cpp and 'VulkanPlayerView_nativeStop' in cpp,
  'AHB C++ compiled via CMake': 'ahb_offscreen.cpp' in cmake,
  'AHB plus required queue-family extension': 'if (canImport)' in cpp and
      'VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME' in cpp,
  'Import Android buffer via Vulkan external memory': 'VkImportAndroidHardwareBufferInfoANDROID' in ahb and 'vkBindImageMemory' in ahb,
  'Query image format support': 'vkGetPhysicalDeviceImageFormatProperties2' in ahb,
  'Record GPU render/copy commands': all(key in ahb for key in ('vkCmdClearColorImage', 'vkCmdCopyBufferToImage', 'vkCmdCopyImage', 'vkCmdPipelineBarrier')),
  'Acquire synchronization waits for transfer': 'VK_PIPELINE_STAGE_TRANSFER_BIT :' in cpp,
  'New APK version': 'versionName = "0.12.0"' in gradle,
  'CI uploads apk and failure logs': 'FrameRoom-r0.12-debug-apk' in workflow and 'FrameRoom-r0.12-build-log' in workflow,
  'AHB ring owns two source frames': 'std::array<AhbOffscreen, 2>' in cpp and 'current.record(' in cpp,
  'Export source buffer accessor': 'hardwareBuffer() const' in (r/'app/src/main/cpp/ahb_offscreen.h').read_text(),
  'Only public Android NDK link libraries': 'nativewindow' not in cmake and 'vulkan android log' in cmake,
  'Bloodframe real JNI initialization': 'bloodframe_probe.cpp' in cmake and
      'bloodframeReport(String dllPath)' in (r/'app/src/main/java/dev/frameroom/NativeProbe.java').read_text() and
      'bb_lsfg_init' in (r/'app/src/main/cpp/bloodframe_probe.cpp').read_text(),
  'Bloodframe built in CI, SHA pinned': 'f4ac713aef58d9403a136e179b3b0d163d85eb66' in workflow and
      'libbloodframe.so' in workflow,
  'Synthetic LSFG batch invokes C API': 'framegen_batch.cpp' in cmake and
      'frameGenBatchReport(String dllPath)' in (r/'app/src/main/java/dev/frameroom/NativeProbe.java').read_text() and
      'bb_lsfg_present' in (r/'app/src/main/cpp/framegen_batch.cpp').read_text(),
  'Private split APK snapshot support': 'splitSourceDirs' in (r/'app/src/main/java/dev/frameroom/GamePackageSnapshot.java').read_text() and
      'getFilesDir()' in (r/'app/src/main/java/dev/frameroom/GamePackageSnapshot.java').read_text(),
  'Graphics process separated': 'android:process=":virtualgfx"' in (r/'app/src/main/AndroidManifest.xml').read_text(),
  'Snapshots are explicitly nonexecutable': 'APK_COPIED_RUNTIME_NOT_AVAILABLE' in
      (r/'app/src/main/java/dev/frameroom/GamePackageSnapshot.java').read_text(),
  'APK host process declared': 'android:process=":virtualgame"' in
      (r/'app/src/main/AndroidManifest.xml').read_text(),
  'Cooperative demo is a separately built APK': 'include(":app", ":samplegame")' in
      (r/'settings.gradle.kts').read_text() and
      'samplegame:assembleDebug' in workflow,
  'Host verifies signed sample package': 'checkSignatures' in
      (r/'app/src/main/java/dev/frameroom/HostedGameActivity.java').read_text() and
      'dev.frameroom.samplegame' in (r/'app/src/main/java/dev/frameroom/HostedGameActivity.java').read_text(),
  'Hosted DEX code is read-only': 'setReadOnly()' in
      (r/'app/src/main/java/dev/frameroom/GamePackageSnapshot.java').read_text() and
      'DexClassLoader' in (r/'app/src/main/java/dev/frameroom/HostedGameActivity.java').read_text(),
  'CI patches gralloc allocation check before building Bloodframe': 'tools/patch_bloodframe_ahb.py' in workflow and
      'f4ac713aef58d9403a136e179b3b0d163d85eb66' in workflow,
  'Native diagnostics distinguish input from output AHB': '"saída FG"' in
      (r/'app/src/main/cpp/framegen_batch.cpp').read_text() and
      'AHardwareBuffer_isSupported' in (r/'app/src/main/cpp/framegen_batch.cpp').read_text(),
  'Live frame verifier reads actual GPU output and gates presentation': 'vkCmdCopyImageToBuffer' in fg and
      'inspectFrames' in fg and 'if (verified &&' in cpp and 'vkCmdBlitImage' in cpp,
  'Only Vulkan display timing is labeled as on-screen FPS': 'vkGetPastPresentationTimingGOOGLE' in cpp and
      'FPS na tela' in cpp and 'envios/s' in cpp,
  'Hosted Vulkan game uses the co-signed APK implementation': 'createVulkanScene' in
      (r/'app/src/main/java/dev/frameroom/HostedGameActivity.java').read_text() and
      'VULKAN_GAME_API' in (r/'samplegame/src/main/AndroidManifest.xml').read_text(),
  'Arena loads its embedded APK through the verified classloader path': 'stageEmbeddedArena' in gradle and
      'loadEntry(path,' in (r/'app/src/main/java/dev/frameroom/HostedGameActivity.java').read_text(),
  'GPU scheduler uses display timestamps and skips late interpolated frames': 'desiredPresentNs' in cpp and
      'pacing::pairTimes(' in cpp and 'render(live ? &frame : nullptr, VK_NULL_HANDLE' in cpp,
  'No AHB image requirements fetched prior to binding': all('vkGetImageMemoryRequirements(' not in source for source in
      (ahb, fg, (r/'app/src/main/cpp/framegen_batch.cpp').read_text())),
  'No MediaProjection API': 'android.media.projection' not in java and 'createScreenCaptureIntent(' not in java and 'AMediaProjection' not in cpp,
  'Parallel installation is delegated to the Android installer inside an owned profile':
      'ACTION_INSTALL_PACKAGE' in space and 'Uri.fromParts("package", pkg, null)' in space and
      'SpaceProfile.isOwned(this)' in space and 'isManagedProfile()' in profile and 'isProfileOwnerApp(' in profile,
}
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
if not all(checks.values()):
    raise SystemExit(1)
print(f'Static wiring: {len(checks)} passed; Android build/device verification still required.')
