#!/usr/bin/env python3
"""
cache_helper.py

Small helper for cache related JSON comparisons.

Core behavior:

    compare_and_update_json(snippet, json_path) -> bool

    - snippet: a JSON compatible object already extracted from manifest
      (dict, list, etc)
    - json_path: path to a JSON file on disk

    Logic:
      1. If json_path exists and its JSON content is structurally identical
         to snippet (after normalization), return True.
      2. Otherwise, write snippet to json_path (creating directories if needed)
         and return False.
      3. If json_path does not exist, create it with snippet and return False.
"""

import hashlib
import json
import logging
import os
import sys
from typing import Any


def _configure_logger() -> logging.Logger:
    """Configure a simple logger for this module."""
    logger = logging.getLogger(__name__)
    if not logger.handlers:
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(logging.Formatter("%(levelname)s: %(message)s"))
        logger.addHandler(handler)
        logger.setLevel(logging.INFO)
    return logger


LOGGER = _configure_logger()


class CacheHelper:
    """Utility to compare and update a JSON cache file."""

    @staticmethod
    def _normalize(obj: Any) -> Any:
        """
        Normalize a JSON compatible object so comparisons are stable.

        For dicts this sorts keys recursively. For lists it normalizes
        each element in order. Primitives are returned as is.
        """
        if isinstance(obj, dict):
            return {k: CacheHelper._normalize(obj[k]) for k in sorted(obj.keys())}
        if isinstance(obj, list):
            return [CacheHelper._normalize(x) for x in obj]
        return obj

    @staticmethod
    def stable_hash(snippet: Any) -> str:
        """
        Produce a stable hash for any JSON-serializable snippet.

        The snippet is normalized first, then hashed with BLAKE2b to a short
        hex string. Useful for lightweight fingerprinting.
        """
        normalized = CacheHelper._normalize(snippet)
        payload = json.dumps(normalized, sort_keys=True, separators=(",", ":"))
        return hashlib.blake2b(payload.encode("utf-8"), digest_size=16).hexdigest()

    @staticmethod
    def _load_json_file(path: str) -> Any:
        """
        Load JSON from file. Returns None on any error.
        """
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except FileNotFoundError:
            return None
        except Exception as exc:
            LOGGER.warning("Failed to read or parse JSON file '%s': %s", path, exc)
            return None

    @staticmethod
    def _write_json_file(path: str, data: Any) -> None:
        """
        Write JSON to file, creating directories if needed.
        """
        try:
            parent = os.path.dirname(os.path.abspath(path))
            if parent and not os.path.exists(parent):
                os.makedirs(parent, exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, sort_keys=True)
        except Exception as exc:
            LOGGER.error("Failed to write JSON file '%s': %s", path, exc)
            raise

    @staticmethod
    def compare_and_update_json(
        snippet: Any, json_path: str, write_if_different: bool = True
    ) -> bool:
        """
        Compare a JSON snippet against the content of json_path.

        Returns:
            True if the existing file content matches the snippet after
            normalization.

            False if the file was missing or did not match. In that case,
            the file is overwritten or created with the snippet content unless
            write_if_different is False.

        Behavior:
            - If json_path does not exist:
                - Write snippet to json_path (unless write_if_different is False)
                - Log info about creation when writing
                - Return False

            - If json_path exists but cannot be parsed as JSON:
                - Log a warning
                - Overwrite with snippet (unless write_if_different is False)
                - Return False

            - If parsed JSON matches snippet (after normalization):
                - Return True

            - If parsed JSON does not match snippet:
                - Overwrite json_path with snippet (unless write_if_different is False)
                - Return False
        """
        snippet_norm = CacheHelper._normalize(snippet)

        existing = CacheHelper._load_json_file(json_path)

        # File does not exist or could not be parsed
        if existing is None:
            if write_if_different:
                LOGGER.info(
                    "JSON cache file '%s' missing or unreadable. Creating or replacing with new snippet.",
                    json_path,
                )
                CacheHelper._write_json_file(json_path, snippet_norm)
            return False

        existing_norm = CacheHelper._normalize(existing)

        if existing_norm == snippet_norm:
            return True

        if write_if_different:
            LOGGER.info(
                "JSON cache file '%s' differs from provided snippet. Updating cache.",
                json_path,
            )
            CacheHelper._write_json_file(json_path, snippet_norm)
        return False


def compare_and_update_json(
    snippet: Any, json_path: str, write_if_different: bool = True
) -> bool:
    """
    Convenience module level wrapper for CacheHelper.compare_and_update_json.

        from cache_helper import compare_and_update_json

        if compare_and_update_json(section, "cache/effects.json"):
            print("Cache is up to date")
        else:
            print("Cache was updated")
    """
    return CacheHelper.compare_and_update_json(snippet, json_path, write_if_different)


def stable_hash(snippet: Any) -> str:
    """Module level wrapper for CacheHelper.stable_hash."""
    return CacheHelper.stable_hash(snippet)


if __name__ == "__main__":
    # Simple manual test:
    #   python cache_helper.py <json_path>
    # It will use a hard coded snippet and compare against the given file.
    if len(sys.argv) < 2:
        print("Usage: python cache_helper.py <json_path>", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    test_snippet = {
        "example": True,
        "values": [1, 2, 3],
    }

    same = compare_and_update_json(test_snippet, path)
    if same:
        print(f"Existing JSON at '{path}' matches snippet.")
    else:
        print(f"JSON at '{path}' was created or updated.")
