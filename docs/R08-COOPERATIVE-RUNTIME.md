# R0.8 cooperative runtime — trusted demo only

This app demonstrates executing Android bytecode originating from an independently
installed APK **inside FrameRoom's separate :virtualgame process**. It is a
**cooperative plugin proof of concept**, NOT a universal Android cloning engine.

The demo APK \`dev.frameroom.samplegame\` exports an entrypoint using the
metadata \`dev.frameroom.GAME_ENTRY\` pointing to a public static Java method
\`createView(Context)\` that returns a View.

Threat model:
- Host requires the exact sample package and same signing certificate.
- Host validates stored APK hashes against both the snapshot metadata and the
  currently installed APK and loads read-only APK copies.
- Code runs under FrameRoom's UID in a secondary process and is NOT a security
  sandbox. No third-party game code is accepted.
- This demo uses Android Canvas, not Vulkan, and does NOT invoke Bloodframe.
- Commercial games generally have no cooperative entrypoint and cannot be
  loaded this way. Rendering their frames through LSFG remains unresolved.
