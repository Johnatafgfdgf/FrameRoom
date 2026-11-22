#!/usr/bin/env python3
"""Apply FrameRoom's Android AHB and synchronization fixes to pinned Bloodframe (MIT)."""
from pathlib import Path
import subprocess
import sys

PIN = "f4ac713aef58d9403a136e179b3b0d163d85eb66"
DEVICE = Path("framegen/src/core/device.cpp")
CONTEXTS = [Path(f"framegen/{v}_src/context.cpp") for v in ("v3.1", "v3.1p")]

PROBE = """bool probeAhbImageUsage(VkPhysicalDevice physicalDevice, VkFormat format,"""
ALLOCATOR = """#ifdef __ANDROID__
// FrameRoom uses SDR RGBA8 at 640x360, independent of the swapchain extent.
// Keep this first allocation probe in sync with the demo/batch's buffer contract.
bool probeFrameRoomAllocation(VkFormat format, uint64_t usage, const char* role) {
    if (format != VK_FORMAT_R8G8B8A8_UNORM)
        return true;
    AHardwareBuffer_Desc desc{};
    desc.width = 640;
    desc.height = 360;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = usage;
    AHardwareBuffer* buffer = nullptr;
    const int supported = AHardwareBuffer_isSupported(&desc);
    const int result = AHardwareBuffer_allocate(&desc, &buffer);
    const bool allocated = result == 0 && buffer != nullptr;
    if (buffer) AHardwareBuffer_release(buffer);
    if (!allocated)
        std::cerr << "Bloodframe gralloc: " << role << " RGBA8 640x360 usage=" << usage
                  << " isSupported=" << supported << " allocate=" << result << '\\n';
    return allocated;
}
#endif

"""
USAGES = """    const bool sampledInput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_SAMPLED_BIT);
    const bool storageOutput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_STORAGE_BIT);
    const bool transferInput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    const bool transferOutput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT);"""
NEGOTIATE = """    constexpr uint64_t baseUsage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
        AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
    const bool baseAllocation = probeFrameRoomAllocation(sharedFormat, baseUsage, "input/copy output");
    const bool sampledInput = baseAllocation && probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_SAMPLED_BIT);
    const bool storageOutput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_STORAGE_BIT) && probeFrameRoomAllocation(sharedFormat,
        baseUsage | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER, "direct output");
    const bool transferInput = baseAllocation && probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    const bool transferOutput = baseAllocation && probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT);"""
EXTENSION = """    requireExtension(availableExtensions, enabledExtensions,
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);"""
SIGNALED = """    const bool hasInputSemaphore =
        inSem >= 0
        || (inSem == -1
            && inSemHandleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);"""
def replace_once(source: str, anchor: str, update: str) -> str:
    if source.count(anchor) != 1 or (update != anchor and update in source):
        raise ValueError(f"Missing, ambiguous or already patched anchor: {anchor.splitlines()[0]}")
    return source.replace(anchor, update, 1)


def patch_device(source: str) -> str:
    source = replace_once(source, "#include <volk.h>",
                          "#include <volk.h>\n#ifdef __ANDROID__\n#include <android/hardware_buffer.h>\n#endif")
    source = replace_once(source, PROBE, ALLOCATOR + PROBE)
    source = replace_once(source, USAGES, NEGOTIATE)
    return replace_once(source, EXTENSION, EXTENSION + """
    requireExtension(availableExtensions, enabledExtensions,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);""")


def patch_context(source: str) -> str:
    # -1 means the host already waited. Importing it as a semaphore would
    # still require VK_KHR_external_semaphore_fd on the host-wait-only path.
    return replace_once(source, SIGNALED, "    const bool hasInputSemaphore = inSem >= 0;")


def self_test() -> None:
    device = "\n".join(("#include <volk.h>", PROBE, USAGES, EXTENSION))
    updated = patch_device(device)
    assert "AHardwareBuffer_allocate(&desc, &buffer)" in updated
    assert "baseAllocation && probeAhbImageUsage" in updated
    assert "baseUsage | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER" in updated
    assert "VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME" in updated
    assert patch_context(SIGNALED).endswith("inSem >= 0;")
    for patch, patched in ((patch_device, updated), (patch_context, patch_context(SIGNALED))):
        try:
            patch(patched)
        except ValueError:
            continue
        raise AssertionError("Double patch unexpectedly accepted")
    print("PASS: Bloodframe gralloc and synchronization patch self-test")


def main(argv: list[str]) -> int:
    if argv[1:] == ["--self-test"]:
        self_test()
        return 0
    if len(argv) != 2:
        print("Usage: patch_bloodframe_ahb.py PATH_TO_PINNED_BLOODFRAME", file=sys.stderr)
        return 2
    root = Path(argv[1])
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise ValueError(f"Expected Bloodframe {PIN}, got {revision}")
    files = {DEVICE: patch_device, **{context: patch_context for context in CONTEXTS}}
    updates = {file: patch((root / file).read_text()) for file, patch in files.items()}
    for file, source in updates.items():
        (root / file).write_text(source)
    print(f"Patched {len(files)} Bloodframe source files at {PIN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
