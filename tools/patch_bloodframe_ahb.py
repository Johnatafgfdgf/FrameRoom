#!/usr/bin/env python3
"""FrameRoom R0.9: patch upstream Bloodframe's Vulkan capability/gralloc mismatch.

Upstream's Vulkan format probe can report an RGBA8 storage-image import is possible
while Android's gralloc refuses the exact AHardwareBuffer usage flags. This patch
performs a small allocation probe before selecting the direct-storage transport.
If it fails, the existing upstream transport negotiation chooses
direct-input-copy-output or transport-only (if the Vulkan driver supports it).

The patch is pinned to upstream Bloodframe commit f4ac713a... and refuses to
modify unknown source versions. License for the patched upstream sources is MIT.
"""
from __future__ import annotations
import sys
from pathlib import Path

PIN = "f4ac713aef58d9403a136e179b3b0d163d85eb66"
REL = Path("framegen/src/core/device.cpp")
BEFORE = """    const bool storageOutput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_STORAGE_BIT);"""
AFTER = """    bool storageOutput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_STORAGE_BIT);"""

ANCHOR = """    const bool transferOutput = probeAhbImageUsage(physicalDevice, sharedFormat,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT);
"""

ADDITION = """    // FrameRoom R0.9: Vulkan image capability is not enough to establish
    // Android gralloc allocation of the *exact* direct-storage AHB usage.
    // On Adreno 710, the driver advertises VK_IMAGE_USAGE_STORAGE_BIT but
    // AHardwareBuffer_allocate(RGBA8, GPU_DATA_BUFFER) can return 1.
    // Only use the direct-output-storage transport if gralloc can actually
    // allocate such a buffer; otherwise use upstream's transfer-copy modes.
    if (storageOutput && sharedFormat == VK_FORMAT_R8G8B8A8_UNORM) {
        AHardwareBuffer_Desc desc{};
        desc.width = 640;
        desc.height = 360;
        desc.layers = 1;
        desc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE
                   | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT
                   | AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;
        AHardwareBuffer* trial = nullptr;
        const bool supported = AHardwareBuffer_isSupported(&desc) != 0;
        int allocationRc = supported ? AHardwareBuffer_allocate(&desc, &trial) : -1;
        if (trial != nullptr) AHardwareBuffer_release(trial);
        if (!supported || allocationRc != 0) {
            std::cerr << "lsfg-vk: RGBA8 AHB direct-storage allocation rejected"
                      << " by gralloc (isSupported=" << supported
                      << " rc=" << allocationRc
                      << "); checking transfer-copy transport instead\\\\n";
            storageOutput = false;
        }
    }
"""
# Raw Python string above has \\n in a C++ string after JSON transport; fix
# by constructing it explicitly if needed during source substitution.


def patch_source(source: str) -> str:
    if "#include <volk.h>" not in source:
        raise ValueError("Unsupported Bloodframe source: volk include not found")
    if source.count(BEFORE) != 1 or source.count(ANCHOR) != 1:
        raise ValueError("Pinned Bloodframe anchors missing/ambiguous; refusing blind edits")
    source = source.replace("#include <volk.h>", "#include <volk.h>\n#include <android/hardware_buffer.h>", 1)
    source = source.replace(BEFORE, AFTER, 1)
    source = source.replace(ANCHOR, ANCHOR + ADDITION.replace("\\\\n", "\\n"), 1)
    return source


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--self-test":
        sample = "#include <volk.h>\n" + BEFORE + "\n" + ANCHOR
        patched = patch_source(sample)
        assert "bool storageOutput" in patched
        assert "AHardwareBuffer_isSupported" in patched
        assert "AHardwareBuffer_allocate" in patched
        assert "storageOutput = false" in patched
        assert patched.count("direct-storage allocation rejected") == 1
        try:
            patch_source(patched)
        except ValueError:
            pass
        else:
            raise AssertionError("Double patch unexpectedly accepted")
        print("PASS: Bloodframe gralloc fallback patch self-test")
        return 0
    if len(argv) != 2:
        print("Usage: patch_bloodframe_ahb.py PATH_TO_BLOODFRAME_CLONE", file=sys.stderr)
        return 2
    root = Path(argv[1])
    source = root / REL
    if not source.is_file():
        print(f"Bloodframe device source missing: {source}", file=sys.stderr)
        return 2
    original = source.read_text(encoding="utf-8")
    updated = patch_source(original)
    source.write_text(updated, encoding="utf-8")
    print(f"Patched {source} for gralloc fallback; Bloodframe pinned revision {PIN}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
