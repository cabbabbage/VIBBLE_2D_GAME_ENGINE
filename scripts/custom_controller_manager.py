#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
from typing import Optional, Tuple
import os
import sys
import subprocess
import re


class CustomControllerManager:
    """Helper to create/open an asset-level custom controller (C++).

    Files live in ENGINE/custom_controllers/<asset>_controller.hpp/.cpp
    The key equals the file base name (e.g., "player_controller").

    Also patches ENGINE/asset/controller_factory.cpp to include and register
    the newly created controller in create_by_key(...).
    """

    def __init__(self, info_path: Path) -> None:
        self.info_path = info_path
        self.asset_name = info_path.parent.name
        self.repo_root = self._find_repo_root_with_engine(info_path)
        self.engine_dir = (
            self.repo_root / "ENGINE"
            if self.repo_root
            else info_path.parents[2] / "ENGINE"
        )
        self.ctrl_dir = self.engine_dir / "custom_controllers"
        self.base_name = f"{self.asset_name}_controller"
        self.hpp = self.ctrl_dir / f"{self.base_name}.hpp"
        self.cpp = self.ctrl_dir / f"{self.base_name}.cpp"

        # controller factory assumed to live with other asset code
        self.factory_dir = self.engine_dir / "asset"
        self.factory_cpp = self.factory_dir / "controller_factory.cpp"
        self.factory_hpp = self.factory_dir / "controller_factory.hpp"

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

    # ---------- public API ----------

    def create(self) -> Tuple[Path, Path]:
        """Create controller files and register them in the controller factory."""
        self.ctrl_dir.mkdir(parents=True, exist_ok=True)
        class_name = self._class_name()

        # --- generate files in expected controller format ---
        hpp = f"""#pragma once

#include "asset/asset_controller.hpp"


class Assets;
class Asset;
class Input;

/*
  {class_name}
  auto generated controller for asset "{self.asset_name}"
  dummy behavior for now
*/
class {class_name} : public AssetController {{
public:
    {class_name}(Assets* assets, Asset* self);
    ~{class_name}() override = default;

    void update(const Input& in) override;

private:
    Assets* assets_ = nullptr;           // non owning
    Asset*  self_   = nullptr;           // non owning
}};
"""
        cpp = f"""#include "custom_controllers/{self.base_name}.hpp"

#include "asset/Asset.hpp"
#include "asset/asset_info.hpp"
#include "core/AssetsManager.hpp"

{class_name}::{class_name}(Assets* assets, Asset* self)
    : assets_(assets)
    , self_(self)
{{}}

void {class_name}::update(const Input& /*in*/) {{

    if (!self_ || !self_->info) return;

    // Ensure a safe starting animation
    auto pick_default = [&]() -> std::string {{
        if (self_->info->animations.count("default")) return "default";
        if (self_->info->animations.count("Default")) return "Default";
        return self_->info->animations.empty() ? std::string()
                                               : self_->info->animations.begin()->first;
    }};

    const std::string cur = self_->get_current_animation();
    if (cur.empty()) {{
        std::string chosen = pick_default();
        if (!chosen.empty() && self_->anim_) {{
            self_->anim_->set_animation_now(chosen);
        }}
    }}

    // Default behavior: idle wander
    if (self_->anim_) {{
        self_->anim_->set_idle(0, 20, 3);
    }}

}}
"""
        self.hpp.write_text(hpp, encoding="utf-8")
        self.cpp.write_text(cpp, encoding="utf-8")

        # --- register in controller factory ---
        self._register_in_controller_factory(class_name)

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

    # ---------- internals ----------

    def _class_name(self) -> str:
        # Convert file base (snake/other) to PascalCase
        parts = [p for p in self.base_name.replace("-", "_").split("_") if p]
        return "".join(s[:1].upper() + s[1:] for s in parts)

    # controller factory patching
    def _register_in_controller_factory(self, class_name: str) -> None:
        """Ensure controller_factory.cpp includes and returns this controller."""
        if not self.factory_cpp.exists():
            # fail softly; user can wire manually if project layout differs
            return

        text = self.factory_cpp.read_text(encoding="utf-8")

        include_line = f'#include "custom_controllers/{self.base_name}.hpp"\n'
        if include_line not in text:
            # insert after last custom_controllers include if present, else after other includes
            lines = text.splitlines(True)
            insert_at = 0
            last_inc = 0
            for i, ln in enumerate(lines):
                if ln.lstrip().startswith("#include"):
                    last_inc = i + 1
                # prefer clustering with other custom_controllers includes
                if 'custom_controllers/' in ln:
                    insert_at = i + 1
            if insert_at == 0:
                insert_at = last_inc
            lines.insert(insert_at, include_line)
            text = "".join(lines)

        # add branch in create_by_key
        key_branch = (
            f' if (key == "{self.base_name}")\n'
            f'  return std::make_unique<{class_name}>(assets_, self);\n'
        )

        # find create_by_key(...) body
        m = re.search(
            r"(std::unique_ptr<\s*AssetController\s*>\s*ControllerFactory::create_by_key\s*\([^)]*\)\s*{\s*)(.*?)(\n})",
            text,
            flags=re.DOTALL,
        )
        if m:
            head, body, tail = m.group(1), m.group(2), m.group(3)

            if key_branch not in body:
                # insert before the first closing '}' of the try block if present,
                # else just before the final 'return nullptr;'
                # try to locate the try { ... } block
                try_block = re.search(r"try\s*{\s*(.*?)}\s*catch", body, flags=re.DOTALL)
                if try_block:
                    tb_head, tb_content, tb_tail = (
                        body[: try_block.start(0)],
                        try_block.group(1),
                        body[try_block.end(1) :],
                    )
                    if key_branch not in tb_content:
                        tb_content = tb_content + key_branch
                    body = tb_head + "try {\n" + tb_content + "}" + tb_tail
                else:
                    # fallback: place before final return nullptr;
                    body = body.replace("return nullptr;", key_branch + " return nullptr;")

            text = head + body + tail

        self.factory_cpp.write_text(text, encoding="utf-8")
