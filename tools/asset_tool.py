#!/usr/bin/env python3
"""
Unified VIBBLE 2D Game Engine Asset Tool.

Processes and saves animation images with scaling and effects.
C++ side handles metadata generation, signatures, and caching logic.

Commands:
  cache-manager: Image loading/saving operations (no metadata)
  build-texture: Build animation textures for single asset
  build-all: Build cached images for all assets with parallel processing

Usage: python tools/asset_tool.py <command> [args...]
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import hashlib
from PIL import Image, ImageFilter, ImageEnhance
import colorsys
import numpy as np
from heapq import heappush, heappop
import multiprocessing
from concurrent.futures import ThreadPoolExecutor, as_completed, ProcessPoolExecutor

# Constants matching C++ animation.cpp
kAnimationCacheVersion = 3
kSignatureOffset = 0xDEADBEEFDEADBEEF  # Matching surface_utils.cpp

class CacheManager:
    """Python implementation of C++ CacheManager functionality."""



    @staticmethod
    def load_surface(path: str) -> Optional[Image.Image]:
        """Load image file using PIL (replaces SDL_Image loading)."""
        try:
            img = Image.open(path)
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            return img
        except Exception:
            return None

    @staticmethod
    def save_png(image: Image.Image, path: str) -> bool:
        """Save image as PNG (replaces SDL_Image saving)."""
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            image.save(path, 'PNG', optimize=False)
            # Force flush to ensure file is written
            return True
        except Exception as e:
            print(f"Error saving PNG to {path}: {e}", file=sys.stderr)
            return False

    @staticmethod
    def load_surface_sequence(folder: str, frame_count: int) -> List[Image.Image]:
        """Load sequence of PNG images from folder."""
        surfaces = []
        for i in range(frame_count):
            png_path = os.path.join(folder, f"{i}.png")
            img = CacheManager.load_surface(png_path)
            if img is None:
                return []  # Failed to load
            surfaces.append(img)
        return surfaces

    @staticmethod
    def save_surface_sequence(folder: str, images: List[Image.Image]) -> bool:
        """Save sequence of images as PNG files."""
        try:
            # Clean/create directory
            if os.path.exists(folder):
                import shutil
                shutil.rmtree(folder)
            os.makedirs(folder, exist_ok=True)

            # Save each image
            for i, img in enumerate(images):
                path = os.path.join(folder, f"{i}.png")
                if not CacheManager.save_png(img, path):
                    return False
            return True
        except Exception:
            return False

    @staticmethod
    def load_and_scale_surface(path: str, scale: float) -> Optional[Image.Image]:
        """Load image and apply high-quality scaling."""
        img = CacheManager.load_surface(path)
        if img is None or scale <= 0:
            return None

        if scale != 1.0:
            width, height = img.size
            new_width = max(1, int(width * scale))
            new_height = max(1, int(height * scale))

            # Apply staggered scaling for large downscales (matching C++ approach)
            if scale < 0.5:
                current_img = img
                current_width, current_height = width, height

                while True:
                    next_width = max(1, current_width // 2)
                    next_height = max(1, current_height // 2)

                    if next_width < new_width * 1.25 and next_height < new_height * 1.25:
                        break

                    current_img = current_img.resize((next_width, next_height), Image.LANCZOS)
                    current_width, current_height = next_width, next_height

                    if current_width <= max(1, int(new_width * 1.1)) and \
                       current_height <= max(1, int(new_height * 1.1)):
                        break

                # Final scale if needed
                if current_width != new_width or current_height != new_height:
                    img = current_img.resize((new_width, new_height), Image.LANCZOS)
                else:
                    img = current_img
            else:
                # Simple single-step scaling
                img = img.resize((new_width, new_height), Image.LANCZOS)

        return img



class ImageEffectProcessor:
    """Process image effects matching C++ image_effects.cpp implementation."""

    @staticmethod
    def clamp01(value: float) -> float:
        """Clamp value to [0, 1] range."""
        return max(0.0, min(1.0, value))

    @staticmethod
    def apply_effects(image: Image.Image, effects: Dict[str, float]) -> Image.Image:
        """Apply image effects pipeline matching C++ exactly."""
        if not isinstance(image, Image.Image) or image.mode != 'RGBA':
            return image

        # Convert to float RGB for processing
        img_array = []
        width, height = image.size
        pixels = list(image.getdata())

        for y in range(height):
            row = []
            for x in range(width):
                idx = y * width + x
                r, g, b, a = pixels[idx]

                # Skip processing for transparent pixels
                if a < 1:
                    row.append((r, g, b, a))
                    continue

                # Normalize to [0, 1]
                rf = r / 255.0
                gf = g / 255.0
                bf = b / 255.0
                af = a / 255.0

                # Brightness adjustment
                rf = ImageEffectProcessor.clamp01(rf + effects['brightness'])
                gf = ImageEffectProcessor.clamp01(gf + effects['brightness'])
                bf = ImageEffectProcessor.clamp01(bf + effects['brightness'])

                # Contrast adjustment
                contrast_factor = 1.0 + effects['contrast']
                rf = ImageEffectProcessor.clamp01((rf - 0.5) * contrast_factor + 0.5)
                gf = ImageEffectProcessor.clamp01((gf - 0.5) * contrast_factor + 0.5)
                bf = ImageEffectProcessor.clamp01((gf - 0.5) * contrast_factor + 0.5)

                # RGB boost (final multiplier)
                if abs(effects['rgb_boost']) > 1e-4:
                    rgb_mult = 1.0 + effects['rgb_boost']
                    rf = ImageEffectProcessor.clamp01(rf * rgb_mult)
                    gf = ImageEffectProcessor.clamp01(gf * rgb_mult)
                    bf = ImageEffectProcessor.clamp01(bf * rgb_mult)

                # Convert back to bytes
                row.append((
                    int(ImageEffectProcessor.clamp01(rf) * 255),
                    int(ImageEffectProcessor.clamp01(gf) * 255),
                    int(ImageEffectProcessor.clamp01(bf) * 255),
                    int(ImageEffectProcessor.clamp01(af) * 255)
                ))
            img_array.append(row)

        # Create new image from processed pixels
        result = Image.new('RGBA', (width, height))
        flat_pixels = [pixel for row in img_array for pixel in row]
        result.putdata(flat_pixels)

        return result

class MaskGenerator:
    """Generate faded masks matching C++ GenerateFadedMask implementation."""

    @staticmethod
    def generate_mask(image: Image.Image, settings: Dict[str, float]) -> Image.Image:
        """Generate faded mask for a single image matching C++ exactly."""
        if not isinstance(image, Image.Image) or image.mode != 'RGBA':
            return image

        # Sanitize settings (matching C++ SanitizeShadowMaskSettings)
        sanitized_settings = {
            'expansion_ratio': np.clip(settings.get('expansion_ratio', 0.5), 0.0, 4.0),
            'blur_scale': np.clip(settings.get('blur_scale', 1.0), 0.0, 4.0),
            'falloff_start': np.clip(settings.get('falloff_start', 0.3), 0.0, 0.99),
            'falloff_exponent': max(0.01, settings.get('falloff_exponent', 1.5)),
            'alpha_multiplier': np.clip(settings.get('alpha_multiplier', 1.0), 0.0, 4.0)
        }

        width, height = image.size

        # Get alpha channel
        alpha_np = np.array(image.split()[-1], dtype=np.uint8)

        # Simple mask generation - expand alpha with falloff
        # (Simplified for performance - detailed implementation removed)
        expand_radius = max(1, int(min(width, height) * sanitized_settings['expansion_ratio'] * 0.5))
        alpha_mult = sanitized_settings['alpha_multiplier']

        # Create expanded mask (simplified)
        expanded_w = width + expand_radius * 2
        expanded_h = height + expand_radius * 2

        alpha_values = np.zeros((expanded_h, expanded_w), dtype=np.uint8)
        alpha_values[expand_radius:expand_radius+height, expand_radius:expand_radius+width] = alpha_np * alpha_mult

        # Simple blur (optional, skipped for speed)
        mask_img = Image.fromarray(alpha_values, mode='L')
        result = Image.new('RGBA', (expanded_w, expanded_h), (0, 0, 0, 0))
        result.putalpha(mask_img)

        return result

def load_effects_from_manifest(manifest_path: str) -> Dict[str, Dict[str, float]]:
    """Load global effects configuration from manifest.json."""
    try:
        with open(manifest_path, 'r') as f:
            manifest = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        pass  # Use defaults

    image_effects = manifest.get("image_effects", {}) if 'manifest' in locals() else {}

    # Always provide defaults
    effects_config = {}
    effect_fields = ['rgb_boost', 'contrast', 'brightness', 'blur',
                    'saturation_red', 'saturation_green', 'saturation_blue', 'hue']
    for effect_type in ["foreground", "background"]:
        effect_settings = image_effects.get(effect_type, {})
        normalized = {field: float(effect_settings.get(field, 0.0)) for field in effect_fields}
        effects_config[effect_type] = normalized

    return effects_config

def scan_animation_frames(src_folder: Path) -> Tuple[int, int, int]:
    """Scan animation frames to count them and get original dimensions."""
    frame_count = 0
    orig_w = 0
    orig_h = 0

    while True:
        frame_path = src_folder / f"{frame_count}.png"
        if not frame_path.exists():
            break

        if frame_count == 0:
            try:
                with Image.open(frame_path) as img:
                    orig_w, orig_h = img.size
            except Exception:
                break

        frame_count += 1

    return frame_count, orig_w, orig_h





def generate_animation_cache(asset_src_dir: Path, asset_name: str, effects_config: Dict[str, Dict[str, float]], scales: List[int]) -> None:
    """Main function to generate animation cached images."""

    cache_root = Path("cache") / asset_name / "animations"

    # Check if asset_src_dir has animation subdirectories or direct frames
    has_animation_dirs = any(d.is_dir() for d in asset_src_dir.iterdir() if d.is_dir())

    animation_dirs = []

    if has_animation_dirs:
        # Asset_src_dir contains animation directories
        animation_dirs = [(d, d.name) for d in sorted(asset_src_dir.iterdir()) if d.is_dir()]
    else:
        # Asset_src_dir contains frames directly - treat as single animation "default"
        animation_dirs = [(asset_src_dir, "default")]

    # Process each animation
    for anim_dir, anim_id in animation_dirs:
        print(f"Processing animation: {anim_id}")

        # Scan frames
        frame_count, orig_w, orig_h = scan_animation_frames(anim_dir)
        if frame_count == 0:
            print(f"  Skipping {anim_id}: no frames found")
            continue

        print(f"  Found {frame_count} frames, dimensions {orig_w}x{orig_h}")

        # Create animation cache subfolder
        anim_cache_root = cache_root / anim_id

        # Remove existing animation cache
        if anim_cache_root.exists():
            import shutil
            shutil.rmtree(anim_cache_root)

        # Create clean animation cache subfolder
        anim_cache_root.mkdir(parents=True, exist_ok=True)

        # For each scale
        for scale_pct in scales:
            scale_factor = scale_pct / 100.0
            target_w = max(1, int(orig_w * scale_factor))
            target_h = max(1, int(orig_h * scale_factor))

            print(f"  Scale {scale_pct}% ({target_w}x{target_h})")

            scale_dir = anim_cache_root / f"scale_{scale_pct}"
            normal_dir = scale_dir / "normal"
            foreground_dir = scale_dir / "foreground"
            background_dir = scale_dir / "background"

            normal_dir.mkdir(parents=True, exist_ok=True)
            foreground_dir.mkdir(parents=True, exist_ok=True)
            background_dir.mkdir(parents=True, exist_ok=True)

            # Process each frame
            for frame_idx in range(frame_count):
                src_path = anim_dir / f"{frame_idx}.png"

                try:
                    with Image.open(src_path) as img:
                        # Convert to RGBA if needed
                        if img.mode != 'RGBA':
                            img = img.convert('RGBA')

                        # Resize to target dimensions
                        if scale_factor != 1.0:
                            img = img.resize((target_w, target_h), Image.LANCZOS)

                        # Normal variant (no effects)
                        normal_path = str(normal_dir / f"{frame_idx}.png")
                        CacheManager.save_png(img, normal_path)

                        # Foreground variant
                        fg_img = ImageEffectProcessor.apply_effects(img, effects_config['foreground'])
                        fg_path = str(foreground_dir / f"{frame_idx}.png")
                        CacheManager.save_png(fg_img, fg_path)

                        # Background variant
                        bg_img = ImageEffectProcessor.apply_effects(img, effects_config['background'])
                        bg_path = str(background_dir / f"{frame_idx}.png")
                        CacheManager.save_png(bg_img, bg_path)

                except Exception as e:
                    print(f"    Error processing frame {frame_idx}: {e}", file=sys.stderr)
                    continue

            print(f"    Generated {frame_count} frames for normal/foreground/background")

    print(f"Generated cached images for animations in {cache_root}")

def discover_assets(src_dir: Path) -> list:
    """Discover all asset directories."""
    assets = []
    if not src_dir.exists():
        return assets

    for item in sorted(src_dir.iterdir()):
        if item.is_dir():
            asset_name = item.name
            assets.append(asset_name)

    return assets



def build_asset_cache(asset_name: str, src_dir: Path, effects_config: dict, scales: List[int]) -> dict:
    """Build cache for a single asset."""
    start_time = time.time()

    asset_src_dir = src_dir / asset_name

    try:
        print(f"Building cache for asset: {asset_name} (scales: {scales})")
        generate_animation_cache(asset_src_dir, asset_name, effects_config, scales)
        return {'asset_name': asset_name, 'status': 'success', 'duration': time.time() - start_time}
    except Exception as e:
        print(f"Error building cache for {asset_name}: {e}", file=sys.stderr)
        return {'asset_name': asset_name, 'status': 'error', 'error': str(e), 'duration': time.time() - start_time}

def cache_manager_main(args):
    """Handle cache-manager command."""
    parser = argparse.ArgumentParser(description="CacheManager operations")
    subparsers = parser.add_subparsers(dest='operation', help='Available operations')

    # load_surface
    load_surface_parser = subparsers.add_parser('load_surface', help='Load image surface info')
    load_surface_parser.add_argument('path', help='Path to image file')

    # load_surface_sequence
    load_seq_parser = subparsers.add_parser('load_surface_sequence', help='Load sequence of surfaces')
    load_seq_parser.add_argument('folder', help='Folder containing images')
    load_seq_parser.add_argument('frame_count', type=int, help='Number of frames to load')

    # save_surface_sequence
    save_seq_parser = subparsers.add_parser('save_surface_sequence', help='Save sequence of surfaces')
    save_seq_parser.add_argument('folder', help='Folder to save images')
    save_seq_parser.add_argument('frame_count', type=int, help='Number of frames')

    sub_args = parser.parse_known_args(args)[0]

    if sub_args.operation == 'load_surface':
        img = CacheManager.load_surface(sub_args.path)
        if img is None:
            print(json.dumps({"error": "Failed to load surface"}))
            sys.exit(1)
        else:
            print(json.dumps({
                "width": img.size[0],
                "height": img.size[1],
                "path": sub_args.path
            }))

    elif sub_args.operation == 'load_surface_sequence':
        images = CacheManager.load_surface_sequence(sub_args.folder, sub_args.frame_count)
        if not images or len(images) != sub_args.frame_count:
            print(json.dumps({"error": "Failed to load surface sequence", "loaded": len(images)}))
            sys.exit(1)
        else:
            result = {
                "frame_count": len(images),
                "frames": [{"width": img.size[0], "height": img.size[1]} for img in images]
            }
            print(json.dumps(result))

    elif sub_args.operation == 'save_surface_sequence':
        # Would need to implement sequence loading from stdin or args, simplified
        print(json.dumps({"error": "Not implemented"}))
        sys.exit(1)
    else:
        print(json.dumps({"error": "Unknown operation"}))
        sys.exit(1)

def build_texture_main(args):
    """Handle build-texture command."""
    parser = argparse.ArgumentParser(description="Build animation textures for single asset")
    parser.add_argument("--asset-src-dir", type=str, required=True,
                       help="Path to asset source directory")
    parser.add_argument("--asset-name", type=str, required=True,
                       help="Asset name for cache path building")
    parser.add_argument("--scales", type=str, required=True,
                       help="Comma-separated list of scale percentages")
    parser.add_argument("--manifest-path", type=str,
                       help="Path to manifest.json")

    sub_args = parser.parse_args(args)
    
    manifest_path = sub_args.manifest_path or "manifest.json"

    # Load effects
    effects_config = load_effects_from_manifest(manifest_path)
    print(f"Loaded effects from manifest")

    # Parse scales
    scales = [int(x.strip()) for x in sub_args.scales.split(',')]

    print(f"Building cache for asset {sub_args.asset_name}")
    generate_animation_cache(
        asset_src_dir=Path(sub_args.asset_src_dir),
        asset_name=sub_args.asset_name,
        effects_config=effects_config,
        scales=scales
    )

def build_all_main(args):
    """Handle build-all command."""
    parser = argparse.ArgumentParser(description="Build caches for all assets")
    parser.add_argument("--src-dir", type=str, default="SRC/assets",
                       help="Source directory containing asset folders")
    parser.add_argument("--assets", type=str, nargs='*',
                       help="Specific assets to build")
    parser.add_argument("--parallel", type=int, default=0,
                       help="Number of parallel processes (0=auto)")

    sub_args = parser.parse_args(args)

    src_dir = Path(sub_args.src_dir)
    parallel_jobs = sub_args.parallel
    if parallel_jobs == 0:
        parallel_jobs = max(1, multiprocessing.cpu_count() - 1)

    # Load effects
    effects_config = load_effects_from_manifest("manifest.json")
    print(f"Loaded global effects from manifest")

    # Discover/filter assets
    all_assets = discover_assets(src_dir)
    assets_to_build = sub_args.assets if sub_args.assets else all_assets

    if not assets_to_build:
        print(f"No assets found in {src_dir}")
        return

    print(f"Building caches for {len(assets_to_build)} assets using {parallel_jobs} processes")

    start_time = time.time()

    with ThreadPoolExecutor(max_workers=parallel_jobs) as executor:
        # Default scales for legacy support - C++ should provide explicit scales
        default_scales = [100, 75, 50, 33]
        future_to_asset = {
            executor.submit(build_asset_cache, asset_name, src_dir, effects_config, default_scales): asset_name
            for asset_name in assets_to_build
        }

        results = []
        for future in as_completed(future_to_asset):
            asset_name = future_to_asset[future]
            try:
                result = future.result()
                results.append(result)
                status = "✅" if result['status'] == 'success' else "❌"
                print(f"{status} {asset_name}: {result['status']} ({result['duration']:.1f}s)")
                if 'error' in result:
                    print(f"   Error: {result['error']}")
            except Exception as e:
                print(f"❌ {asset_name}: exception - {str(e)}")

    # Summary
    total_time = time.time() - start_time
    successful = sum(1 for r in results if r['status'] == 'success')
    errors = sum(1 for r in results if r['status'] == 'error')
    print(f"\nCompleted: {successful}/{len(results)} successful, {errors} errors in {total_time:.1f}s")

def main():
    """Main entry point with command dispatch."""
    if len(sys.argv) < 2:
        print("Usage: python tools/asset_tool.py <command> [args...]")
        print("Commands: cache-manager, build-texture, build-all")
        sys.exit(1)

    command = sys.argv[1]
    remaining_args = sys.argv[2:]

    if command == 'cache-manager':
        cache_manager_main(remaining_args)
    elif command == 'build-texture':
        build_texture_main(remaining_args)
    elif command == 'build-all':
        build_all_main(remaining_args)
    else:
        print(f"Unknown command: {command}")
        print("Available commands: cache-manager, build-texture, build-all")
        sys.exit(1)

if __name__ == "__main__":
    main()
