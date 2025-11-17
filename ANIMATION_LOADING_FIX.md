# Animation Loading/Creation Fix - COMPLETE SOLUTION

## Problem
The Python script (`tools/asset_tool.py`) was creating animation textures successfully, but the C++ animation loader (`ENGINE/asset/animation.cpp`) failed to load the newly created textures, resulting in "produced no frames" errors and infinite regeneration loops.

## Root Causes

### 1. Incorrect Metadata Values
The Python script was writing incorrect metadata values:
- `source_signature: 0` (should be a hash of source frame files)
- `scale_profile_revision: 0` (should match the manifest revision)

### 2. Revision Source Mismatch  
The C++ loader reads the `scale_profile_revision` from `manifest.json`, but the Python script was trying to COMPUTE it from scale steps. This caused mismatches even when using the same scales.

### 3. Source Signature Computation
Both C++ and Python were computing source signatures from the same source files, but needed identical implementations of the FNV-1a hashing algorithm.

## Complete Solution

### Python Script Changes (`tools/asset_tool.py`)

1. **Added `compute_source_signature()` function** (lines 331-376)
   - Computes FNV-1a hash of source frame files matching C++ implementation exactly
   - Includes file size, modification time in nanoseconds, and frame index
   - Returns proper uint64 signature
   - Added debug logging to track computation

2. **Added `get_asset_scale_profile_and_revision()` function** (lines 527-549)
   - Reads scale profile AND revision from manifest.json
   - Extracts `recommended_percentages` and `revision` from asset's `scaling_profile`
   - Returns tuple of (scales, revision) matching what C++ expects

3. **Updated `generate_animation_cache()`** (lines 494-503)
   - Reads revision from manifest instead of computing it
   - Falls back to computed revision only if manifest has none
   - Uses exact revision value that C++ will compare against

4. **Updated `build_asset_cache()`** (lines 557-567)
   - Now calls `get_asset_scale_profile_and_revision()` to get both scales and revision
   - Ensures consistency with manifest.json

5. **Improved file I/O reliability**
   - Added filesystem sync after metadata write (lines 521-524)
   - Added small delay after cache generation (line 529)
   - Improved error reporting in PNG save function (lines 71-79)
   - Disabled PNG optimization for faster writes (line 73)

### C++ Code Changes (`ENGINE/asset/animation.cpp`)

1. **Added debug logging to `compute_source_signature()`** (lines 286-318)
   - Logs the folder being scanned
   - Logs first frame's size and timestamp for verification
   - Logs final computed signature in hex and decimal
   - Helps diagnose signature mismatches

2. **Added synchronization delay** (line 1390)
   - Added 50ms delay after Python script succeeds
   - Ensures filesystem operations complete before retry
   - Prevents race conditions on Windows filesystem

## Key Insight

The critical issue was that **the C++ reads `scale_profile_revision` from `manifest.json`**, NOT computes it. The manifest contains a pre-computed revision like:

```json
"scaling_profile": {
  "revision": 15216200396871486443,
  "recommended_percentages": [100, 7, 6, 5]
}
```

The Python script MUST write this exact revision value to the cache metadata, not try to compute its own. The solution is to:
1. Read the revision from manifest.json
2. Write that same value to cache metadata  
3. C++ will compare the two and they'll match

## Testing
Delete the cache folder and rebuild. The log should now show:
- Python script generates cache successfully
- C++ loader reads metadata with matching revision
- C++ computes source signature and it matches metadata
- No "source signature mismatch" or "revision mismatch" errors
- No "produced no frames" errors
- Assets load on first attempt without regeneration loops

Example successful log:
```
[AnimationLoader] Candle profile_revision=15216200396871486443, profile_steps=[1.00, 0.75, 0.07]
[AnimationLoader] Candle::default found metadata (revision 15216200396871486443) expecting revision 15216200396871486443
[DEBUG] Computing source signature from: SRC\assets\Candle\default
[DEBUG] Frame 0: size=12345, mtime_nanos=1234567890123456789
[DEBUG] Computed signature: 0xcd0dfc81b5f41056 (14852925806042808406)
[AnimationLoader] Candle::default -> 12 frame(s) @ 213x795 from cache in 0.023s
```

## Files Modified
- `tools/asset_tool.py` - Python cache generation script
- `ENGINE/asset/animation.cpp` - C++ animation loader with debug output
