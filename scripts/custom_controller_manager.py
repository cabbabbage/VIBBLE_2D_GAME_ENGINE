#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
from typing import Optional, Tuple
import os
import sys
import subprocess


class CustomControllerManager:
    """Helper to create/open an asset-level custom controller (C++).

    Files live in ENGINE/custom_controllers/<asset>_controller.hpp/.cpp
    The key equals the file base name (e.g., "player_controller").
    """

    def __init__(self, info_path: Path) -> None:
        self.info_path = info_path
        self.asset_name = info_path.parent.name
        self.repo_root = self._find_repo_root_with_engine(info_path)
        self.engine_dir = self.repo_root / "ENGINE" if self.repo_root else info_path.parents[2] / "ENGINE"
        self.ctrl_dir = self.engine_dir / "custom_controllers"
        self.base_name = f"{self.asset_name}_controller"
        self.hpp = self.ctrl_dir / f"{self.base_name}.hpp"
        self.cpp = self.ctrl_dir / f"{self.base_name}.cpp"

    @staticmethod
    def _find_repo_root_with_engine(start: Path) -> Optional[Path]:
        for p in [start] + list(start.parents):
            if (p / "ENGINE").exists() and (p / "ENGINE").is_dir():
                return p
        return None

    def exists(self) -> bool:
        return self.hpp.exists() and self.cpp.exists()

    def key(self) -> str:
        return self.base_name

    def create(self) -> Tuple[Path, Path]:
        self.ctrl_dir.mkdir(parents=True, exist_ok=True)
        class_name = self._class_name()
        hpp = f"""#pragma once
#include <string>

class {class_name} {{
public:
    // Unique key for this controller
    std::string key = "{self.base_name}";

    {class_name}();
    void update(float dt);

private:
    // TODO: add state here
}};
"""
        cpp = f"""#include "{self.base_name}.hpp"

{class_name}::{class_name}() {{
    // TODO: initialize state
}}

void {class_name}::update(float dt) {{
    (void)dt;
    // TODO: per-frame logic
}}
"""
        self.hpp.write_text(hpp, encoding="utf-8")
        self.cpp.write_text(cpp, encoding="utf-8")
        # Ensure engine UI/loader include this controller header
        try:
            self._ensure_includes()
        except Exception:
            pass
        return self.hpp, self.cpp

    def open_in_ide(self) -> None:
        path = self.hpp if self.hpp.exists() else self.cpp
        if not path:
            return
        try:
            if sys.platform.startswith("win"):
                os.startfile(str(path))  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", str(path)])
            else:
                subprocess.Popen(["xdg-open", str(path)])
        except Exception:
            pass

    def _class_name(self) -> str:
        # Convert file base (snake/other) to PascalCase
        parts = [p for p in self.base_name.replace('-', '_').split('_') if p]
        return ''.join(s[:1].upper() + s[1:] for s in parts)

    # ----- include injection -----
    def _ensure_includes(self) -> None:
        include_line = f"#include \"custom_controllers/{self.base_name}.hpp\"\n"
        # asset info ui
        ui_cpp = self.engine_dir / "ui" / "asset_info_ui.cpp"
        self._ensure_include_in_file(ui_cpp, include_line)
        # animation loader
        al_cpp = self.engine_dir / "asset_info_methods" / "animation_loader.cpp"
        al_hpp = self.engine_dir / "asset_info_methods" / "animation_loader.hpp"
        self._ensure_include_in_file(al_cpp, include_line)
        self._ensure_include_in_file(al_hpp, include_line)

    @staticmethod
    def _ensure_include_in_file(path: Path, include_line: str) -> None:
        try:
            if not path.exists():
                return
            text = path.read_text(encoding="utf-8")
            if include_line.strip() in text:
                return
            lines = text.splitlines(True)
            # find last include line to insert after
            insert_at = 0
            for i, ln in enumerate(lines[:100]):  # only scan header region
                if ln.lstrip().startswith("#include"):
                    insert_at = i + 1
            lines.insert(insert_at, include_line)
            path.write_text("".join(lines), encoding="utf-8")
        except Exception:
            pass
