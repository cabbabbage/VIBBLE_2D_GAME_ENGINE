#!/usr/bin/env python3
"""
Test script to verify animation cache generation and structure.
"""

import json
import sys
from pathlib import Path

def check_cache_structure(asset_name: str, trigger: str, expected_frames: int, expected_scales: list):
    """Verify cache structure for a given asset/trigger."""
    
    cache_root = Path("cache") / asset_name / "animations" / trigger
    
    print(f"\n=== Checking cache for {asset_name}::{trigger} ===")
    print(f"Cache path: {cache_root}")
    
    if not cache_root.exists():
        print(f"❌ Cache directory does not exist: {cache_root}")
        return False
    
    # Check .ready marker
    ready_marker = cache_root / ".ready"
    if not ready_marker.exists():
        print(f"⚠️  Warning: .ready marker not found")
    else:
        frame_count = ready_marker.read_text().strip()
        print(f"✅ .ready marker found (frame_count: {frame_count})")
    
    # Check each scale folder
    all_good = True
    for scale_pct in expected_scales:
        scale_dir = cache_root / f"scale_{scale_pct}"
        
        if not scale_dir.exists():
            print(f"❌ Missing scale folder: scale_{scale_pct}")
            all_good = False
            continue
        
        # Check variant folders
        for variant in ['normal', 'foreground', 'background']:
            variant_dir = scale_dir / variant
            
            if not variant_dir.exists():
                print(f"❌ Missing variant folder: scale_{scale_pct}/{variant}")
                all_good = False
                continue
            
            # Check frame files
            missing_frames = []
            for i in range(expected_frames):
                frame_file = variant_dir / f"{i}.png"
                if not frame_file.exists():
                    missing_frames.append(i)
            
            if missing_frames:
                print(f"❌ scale_{scale_pct}/{variant} missing frames: {missing_frames}")
                all_good = False
            else:
                print(f"✅ scale_{scale_pct}/{variant} has all {expected_frames} frames")
    
    return all_good


def check_manifest_scales(asset_name: str):
    """Check what scales are defined in manifest for an asset."""
    
    try:
        with open("manifest.json", "r") as f:
            manifest = json.load(f)
        
        if asset_name in manifest.get("assets", {}):
            asset_data = manifest["assets"][asset_name]
            if "scaling_profile" in asset_data:
                profile = asset_data["scaling_profile"]
                if "recommended_percentages" in profile:
                    scales = profile["recommended_percentages"]
                    print(f"\n📋 Manifest scales for {asset_name}: {scales}")
                    return scales
        
        print(f"\n📋 No scaling profile in manifest for {asset_name}, using defaults: [100, 75, 50]")
        return [100, 75, 50]
    
    except Exception as e:
        print(f"⚠️  Error reading manifest: {e}")
        return [100, 75, 50]


def main():
    if len(sys.argv) < 2:
        print("Usage: python test_animation_cache.py <asset_name> [trigger] [expected_frames]")
        print("Example: python test_animation_cache.py Candle default 12")
        sys.exit(1)
    
    asset_name = sys.argv[1]
    trigger = sys.argv[2] if len(sys.argv) > 2 else "default"
    expected_frames = int(sys.argv[3]) if len(sys.argv) > 3 else None
    
    # Get scales from manifest
    expected_scales = check_manifest_scales(asset_name)
    
    # Auto-detect frame count if not provided
    if expected_frames is None:
        cache_root = Path("cache") / asset_name / "animations" / trigger
        if cache_root.exists():
            ready_marker = cache_root / ".ready"
            if ready_marker.exists():
                try:
                    expected_frames = int(ready_marker.read_text().strip())
                    print(f"📊 Auto-detected frame count from .ready: {expected_frames}")
                except:
                    pass
        
        if expected_frames is None:
            # Try counting from first scale's normal folder
            for scale_pct in expected_scales:
                normal_dir = cache_root / f"scale_{scale_pct}" / "normal"
                if normal_dir.exists():
                    frames = list(normal_dir.glob("*.png"))
                    if frames:
                        expected_frames = len(frames)
                        print(f"📊 Auto-detected frame count from files: {expected_frames}")
                        break
        
        if expected_frames is None:
            print("❌ Could not determine expected frame count")
            sys.exit(1)
    
    # Check structure
    success = check_cache_structure(asset_name, trigger, expected_frames, expected_scales)
    
    if success:
        print(f"\n✅ All checks passed for {asset_name}::{trigger}")
        sys.exit(0)
    else:
        print(f"\n❌ Some checks failed for {asset_name}::{trigger}")
        sys.exit(1)


if __name__ == "__main__":
    main()
