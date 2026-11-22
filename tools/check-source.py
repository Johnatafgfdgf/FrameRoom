#!/usr/bin/env python3
"""Static wiring checks; not evidence that Vulkan GPU calls work."""
from pathlib import Path
r = Path(__file__).resolve().parents[1]
cpp = (r/'app/src/main/cpp/vulkan_demo.cpp').read_text()
ahb = (r/'app/src/main/cpp/ahb_offscreen.cpp').read_text()
java = (r/'app/src/main/java/dev/frameroom/VulkanDemoActivity.java').read_text()
cmake = (r/'app/src/main/cpp/CMakeLists.txt').read_text()
gradle = (r/'app/build.gradle.kts').read_text()
workflow = (r/'.github/workflows/android-debug.yml').read_text()
checks = {
  'JNI signature matches mode boolean': 'nativeStart(Surface surface, int fps, boolean ahb)' in java and
      'nativeStart(JNIEnv* env, jclass, jobject surface, jint fps, jboolean ahb)' in cpp,
  'AHB C++ compiled via CMake': 'ahb_offscreen.cpp' in cmake,
  'Device extension enabled only when AHB requested': 'deviceInfo.enabledExtensionCount = ahbMode ? 2 : 1;' in cpp,
  'Import Android buffer via Vulkan external memory': 'VkImportAndroidHardwareBufferInfoANDROID' in ahb and 'vkBindImageMemory' in ahb,
  'Query image format support': 'vkGetPhysicalDeviceImageFormatProperties2' in ahb,
  'Record GPU render/copy commands': all(key in ahb for key in ('vkCmdClearColorImage', 'vkCmdCopyBufferToImage', 'vkCmdCopyImage', 'vkCmdPipelineBarrier')),
  'Acquire synchronization waits for transfer': 'VK_PIPELINE_STAGE_TRANSFER_BIT :' in cpp,
  'New APK version': 'versionName = "0.4.0"' in gradle,
  'CI uploads apk and failure logs': 'FrameRoom-r0.4-debug-apk' in workflow and 'FrameRoom-r0.4-build-log' in workflow,
  'AHB ring owns two source frames': 'std::array<AhbOffscreen, 2>' in cpp and 'current.record(' in cpp,
  'Export source buffer accessor': 'hardwareBuffer() const' in (r/'app/src/main/cpp/ahb_offscreen.h').read_text(),
  'Only public Android NDK link libraries': 'nativewindow' not in cmake and 'vulkan android log' in cmake,
  'No MediaProjection API': 'android.media.projection' not in java and 'createScreenCaptureIntent(' not in java and 'AMediaProjection' not in cpp,
}
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
if not all(checks.values()):
    raise SystemExit(1)
print(f'Static wiring: {len(checks)} passed; Android build/device verification still required.')
