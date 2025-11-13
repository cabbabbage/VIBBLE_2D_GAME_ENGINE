Depth-Cue Caching and Cold-Start Gating

Overview

- Cold-start warmup disables depth-cue effects (color tint + blur) for a few frames after SceneRenderer initialization to avoid stalling when leaving the loading screen.
- Tinted textures are memoized per (source SDL_Texture*, saturation%, primary%) and reused while the source texture remains valid.
- Gaussian blur kernels are cached per integer blur radius and reused across draws.

Cold-Start Warmup

- For the first N frames after SceneRenderer creation, depth-cue calculations are gated off so only base sprites render.
- Default frames: 8. Override via env var `VIBBLE_DEPTHCUE_WARMUP_FRAMES` (0–120).
- Gating applies to blur and color adjustments (brightness, saturation, primary boost), preventing CPU-side texture reads during early frames.

Tinted Texture Cache

- Key: `(source_texture_pointer, round(saturation_percent), round(primary_percent))` with clamping to [-50, 50].
- On cache hit, the existing tinted SDL_Texture is reused; on miss, it is built once using CPU-side pixels and stored.
- Simple TTL pruning removes entries not used for ~240 frames to prevent unbounded growth.
- Cached entries are destroyed in `SceneRenderer` destructor.

Gaussian Kernel Cache

- Kernels are built once per integer pixel radius (0–64) and reused.
- Eliminates per-draw `build_gaussian_kernel` calls.

Notes and Future Work

- The color adjustment path currently uses CPU-side `SDL_RenderReadPixels`. Moving these adjustments to a shader/uniform would be preferable for performance.
- The current cache design scopes entries to the source `SDL_Texture*` pointer. If source textures are re-created with the same content, their pointer changes will naturally re-key the cache.

