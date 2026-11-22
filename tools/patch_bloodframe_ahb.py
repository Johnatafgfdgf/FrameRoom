#!/usr/bin/env python3
"""Apply FrameRoom's Android AHB and synchronization fixes to pinned Bloodframe (MIT)."""
from pathlib import Path
import subprocess
import sys

PIN = "f4ac713aef58d9403a136e179b3b0d163d85eb66"
DEVICE = Path("framegen/src/core/device.cpp")
C_API = Path("android/bb_lsfg.cpp")
CONTEXTS = [Path(f"framegen/{v}_src/context.cpp") for v in ("v3.1", "v3.1p")]

TARGET = """using namespace LSFG::Core;"""
TARGET_SETUP = """
#ifdef __ANDROID__
namespace {
thread_local uint32_t frameRoomWidth = 640;
thread_local uint32_t frameRoomHeight = 360;
}

// The legacy batch diagnostic keeps 640x360. An active render session asks
// the gralloc probe to check the exact RGBA8 dimensions it will allocate.
extern "C" void frameroom_set_ahb_probe_extent(uint32_t width, uint32_t height) {
    frameRoomWidth = width ? width : 640;
    frameRoomHeight = height ? height : 360;
}
#endif
"""
PROBE = """bool probeAhbImageUsage(VkPhysicalDevice physicalDevice, VkFormat format,"""
ALLOCATOR = """#ifdef __ANDROID__
// FrameRoom shares SDR RGBA8 buffers independent of the swapchain extent.
bool probeFrameRoomAllocation(VkFormat format, uint64_t usage, const char* role) {
    if (format != VK_FORMAT_R8G8B8A8_UNORM)
        return true;
    AHardwareBuffer_Desc desc{};
    desc.width = frameRoomWidth;
    desc.height = frameRoomHeight;
    desc.layers = 1;
    desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    desc.usage = usage;
    AHardwareBuffer* buffer = nullptr;
    const int supported = AHardwareBuffer_isSupported(&desc);
    const int result = AHardwareBuffer_allocate(&desc, &buffer);
    const bool allocated = result == 0 && buffer != nullptr;
    if (buffer) AHardwareBuffer_release(buffer);
    if (!allocated)
        std::cerr << "Bloodframe gralloc: " << role << " RGBA8 " << desc.width << 'x' << desc.height
                  << " usage=" << usage
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
    source = replace_once(source, TARGET, TARGET + "\n" + TARGET_SETUP)
    source = replace_once(source, PROBE, ALLOCATOR + PROBE)
    source = replace_once(source, USAGES, NEGOTIATE)
    return replace_once(source, EXTENSION, EXTENSION + """
    requireExtension(availableExtensions, enabledExtensions,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);""")


def patch_api(source: str) -> str:
    # The static framegen archive is linked with --exclude-libs,ALL.
    # The entrypoint must live in the shared library's C API translation unit.
    anchor = 'extern "C" {\n\nstatic int init_backend'
    return replace_once(source, anchor, """extern "C" {

void frameroom_set_ahb_probe_extent(uint32_t width, uint32_t height);
BB_LSFG_API void bb_lsfg_frameroom_probe_extent(uint32_t width, uint32_t height) {
    frameroom_set_ahb_probe_extent(width, height);
}

static int init_backend""")


def patch_context(source: str) -> str:
    # -1 means the host already waited. Importing it as a semaphore would
    # still require VK_KHR_external_semaphore_fd on the host-wait-only path.
    source = replace_once(source, SIGNALED, "    const bool hasInputSemaphore = inSem >= 0;")
    export = "    const size_t completedWindow ="
    source = replace_once(source, export, """    exportAndroidSyncFdOutputs =
        exportAndroidSyncFdOutputs && vk.device.getDiagnostics().externalSemaphoreSyncFd;
""" + export)
    start = source.index("bool Context::waitForCompletion(Vulkan& vk) {")
    block = source.index("\n", start) + 1
    end = source.index("    return true;", block)
    # Device loss stops all pending work. Teardown can reclaim the context.
    body = source[block:end]
    wrapped = "    try {\n" + "".join("    " + line for line in body.splitlines(keepends=True))
    wrapped += """    } catch (const LSFG::vulkan_error& error) {
        if (error.error() != VK_ERROR_DEVICE_LOST) throw;
    }
"""
    return source[:block] + wrapped + source[end:]


def self_test() -> None:
    device = "\n".join(("#include <volk.h>", TARGET, PROBE, USAGES, EXTENSION))
    updated = patch_device(device)
    assert "AHardwareBuffer_allocate(&desc, &buffer)" in updated
    assert "desc.width = frameRoomWidth" in updated
    assert "frameroom_set_ahb_probe_extent" in updated
    api = patch_api('extern "C" {\n\nstatic int init_backend')
    assert "BB_LSFG_API void bb_lsfg_frameroom_probe_extent" in api
    assert "baseAllocation && probeAhbImageUsage" in updated
    assert "baseUsage | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER" in updated
    assert "VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME" in updated
    context = "\n".join((SIGNALED, "    const size_t completedWindow = n;",
        "bool Context::waitForCompletion(Vulkan& vk) {", "    work();", "    return true;", "}"))
    patched_context = patch_context(context)
    assert "const bool hasInputSemaphore = inSem >= 0;" in patched_context
    assert "getDiagnostics().externalSemaphoreSyncFd" in patched_context
    assert "VK_ERROR_DEVICE_LOST" in patched_context
    for patch, patched in ((patch_device, updated), (patch_context, patched_context)):
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
    files = {DEVICE: patch_device, C_API: patch_api,
             **{context: patch_context for context in CONTEXTS}}
    updates = {file: patch((root / file).read_text()) for file, patch in files.items()}
    for file, source in updates.items():
        (root / file).write_text(source)
    print(f"Patched {len(files)} Bloodframe source files at {PIN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
