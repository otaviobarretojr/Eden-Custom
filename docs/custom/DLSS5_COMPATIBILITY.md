# DLSS 5 Compatibility Filter

This branch experiments with a capture-card-style DLSS 5 Neural Rendering path inside Eden's Vulkan renderer.

## Goal

Keep the normal renderer untouched when the feature is disabled, then insert an optional compatibility path immediately after Eden has rendered the final presentation image and before that image is presented.

Target flow:

```
Switch guest frame
  -> Eden Vulkan composition / existing display filters
  -> final presentation Frame
  -> DLSS 5 compatibility stage
  -> Present
```

## Stage 1: final-frame tap

The first implementation intentionally does not modify image pixels.

It:

- detects the NVIDIA proprietary Vulkan driver;
- requires a Vulkan 1.2-capable adapter for the future Streamline path;
- hooks the final `Frame` used by `RendererVulkan::Composite`;
- records frame dimensions only when they change;
- leaves Streamline and Neural Rendering evaluation disabled.

This gives later Streamline work a stable insertion point without risking the normal presentation path.

## Next stages

1. Bootstrap the NVIDIA Streamline 2.14 runtime before Vulkan device creation.
2. Query the Neural Rendering feature requirements and enable only the required Vulkan capabilities.
3. Create a compatibility input provider for final color plus generated/estimated auxiliary inputs.
4. Evaluate Neural Rendering into a separate output image.
5. Fall back to the untouched Eden frame on every unsupported or failed path.
6. Add an explicit UI setting; default remains Off.

No NVIDIA SDK binaries are committed to this repository.
