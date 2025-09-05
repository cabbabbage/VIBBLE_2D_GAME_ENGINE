#!/usr/bin/env python3
from __future__ import annotations
from typing import Callable, Dict, Optional

import os
import re
import sys
import subprocess
import tkinter as tk
from tkinter import ttk, filedialog, messagebox


class CustomUpdatesPanel:
    """
    Compact panel for per-animation Custom Animation Controller.

    Fields stored in payload:
      - has_custom_animation_controller: bool
      - custom_animation_controller_hpp_path: str (absolute or project-relative)
      - custom_animation_controller_key: str (controller key)

    API:
      - set_values(payload: dict)
      - get_values() -> dict
    """

    def __init__(
        self,
        parent: tk.Widget,
        payload: Dict,
        *,
        on_changed: Optional[Callable[[], None]] = None,
        asset_folder: Optional[str] = None,
        get_current_name: Optional[Callable[[], str]] = None,
    ) -> None:
        self.parent = parent
        self._on_changed = on_changed

        self.asset_folder = asset_folder or ""
        self._get_current_name = get_current_name or (lambda: "")

        self.frame = ttk.LabelFrame(parent, text="Custom animation controller")

        # toggle
        # migrate legacy flag if present
        legacy_flag = bool(payload.get("has_custom_tick_update", False))
        self.enabled_var = tk.BooleanVar(value=bool(payload.get("has_custom_animation_controller", legacy_flag)))
        self.chk = ttk.Checkbutton(
            self.frame, text="Has custom animation controller", variable=self.enabled_var,
            command=self._on_toggle
        )
        self.chk.grid(row=0, column=0, sticky="w", padx=6, pady=(6, 2))

        # row for path/key controls
        self.row = ttk.Frame(self.frame)
        self.row.grid(row=1, column=0, sticky="ew", padx=6, pady=(4, 8))
        self.frame.columnconfigure(0, weight=1)

        # hpp path + key (migrate legacy if present)
        legacy_path = str(payload.get("custom_update_hpp_path", "") or "")
        legacy_key = str(payload.get("custom_update_key", "") or "")
        self.hpp_path: str = str(payload.get("custom_animation_controller_hpp_path", legacy_path) or "")
        self.key_var = tk.StringVar(value=str(payload.get("custom_animation_controller_key", legacy_key) or ""))

        self.btn = ttk.Button(self.row, text=self._btn_text(), command=self._on_button)
        self.btn.pack(side="left")
        ttk.Label(self.row, text="custom_animation_controller_key:").pack(side="left", padx=(12, 4))
        self.key_label = ttk.Label(self.row, textvariable=self.key_var)
        self.key_label.pack(side="left")

        self._apply_row_visibility()

    def get_frame(self) -> ttk.Frame:
        return self.frame

    def set_values(self, payload: Dict) -> None:
        legacy_flag = bool(payload.get("has_custom_tick_update", False))
        self.enabled_var.set(bool(payload.get("has_custom_animation_controller", legacy_flag)))
        legacy_path = str(payload.get("custom_update_hpp_path", "") or "")
        legacy_key = str(payload.get("custom_update_key", "") or "")
        self.hpp_path = str(payload.get("custom_animation_controller_hpp_path", legacy_path) or "")
        self.key_var.set(str(payload.get("custom_animation_controller_key", legacy_key) or ""))
        self.btn.configure(text=self._btn_text())
        self._apply_row_visibility()

    def get_values(self) -> Dict[str, object]:
        return {
            "has_custom_animation_controller": bool(self.enabled_var.get()),
            "custom_animation_controller_hpp_path": self.hpp_path,
            "custom_animation_controller_key": self.key_var.get(),
        }

    # ----- internals -----
    def _btn_text(self) -> str:
        return "Open controller in IDE" if self.hpp_path else "Create controller"

    def _on_toggle(self):
        self._apply_row_visibility()
        self._notify_changed()

    def _apply_row_visibility(self):
        try:
            if self.enabled_var.get():
                self.row.pack_propagate(False)
                self.row.grid()
            else:
                self.row.grid_remove()
        except Exception:
            pass

    def _on_button(self):
        # If controller exists, open; otherwise create both files and set key/path
        if self.hpp_path and os.path.isfile(self.hpp_path):
            self._open_in_editor(self.hpp_path)
            return
        base_name = self._controller_base_name()
        if not base_name:
            messagebox.showerror("Missing name", "Animation ID or asset folder not set.")
            return
        hpp, cpp = self._ensure_controller_files(base_name)
        if not hpp:
            return
        self.hpp_path = hpp
        self.key_var.set(base_name)
        self.btn.configure(text=self._btn_text())
        self._notify_changed()

    def _notify_changed(self):
        if self._on_changed:
            try:
                self._on_changed()
            except Exception:
                pass

    # ----- utility -----
    def _open_in_editor(self, path: str):
        try:
            if sys.platform.startswith("win"):
                os.startfile(path)  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                subprocess.Popen(["open", path])
            else:
                subprocess.Popen(["xdg-open", path])
        except Exception as e:
            messagebox.showerror("Open error", f"Failed to open {path}:\n{e}")

    def _extract_and_set_key(self, path: str):
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
        except Exception as e:
            messagebox.showerror("Read error", f"Failed to read {path}:\n{e}")
            return

        # Find public: section (naive but effective for typical headers)
        pub_idx = content.find("public:")
        if pub_idx < 0:
            pub_block = content
        else:
            tail = content[pub_idx:]
            # stop at next access specifier or class end
            m = re.search(r"\b(private|protected)\s*:\s*|\};", tail)
            pub_block = tail[: m.start()] if m else tail

        mkey = re.search(r"std::string\s+key\s*=\s*\"([^\"]*)\"", pub_block)
        if not mkey:
            # try without explicit std::, or using '=' with single quotes
            mkey = re.search(r"string\s+key\s*=\s*\"([^\"]*)\"", pub_block)
        if mkey:
            self.key_var.set(mkey.group(1))
        else:
            # no key found; inform the user but keep path
            messagebox.showwarning(
                "Key not found",
                "Could not find: std::string key = \"...\"; in public section."
            )
        
    # ----- controller file helpers -----
    def _controller_base_name(self) -> str:
        anim = self._get_current_name() or ""
        asset = os.path.basename(os.path.normpath(self.asset_folder)) if self.asset_folder else ""
        if not anim or not asset:
            return ""
        return f"{asset}_{anim}_controller"

    def _find_repo_root_with_engine(self, start: str) -> Optional[str]:
        p = os.path.abspath(start)
        while True:
            if os.path.isdir(os.path.join(p, "ENGINE")):
                return p
            parent = os.path.dirname(p)
            if parent == p:
                return None
            p = parent

    def _ensure_controller_files(self, base_name: str) -> tuple[str, str]:
        root = self._find_repo_root_with_engine(self.asset_folder or os.getcwd())
        engine_dir = os.path.join(root, "ENGINE") if root else None
        if not engine_dir:
            messagebox.showerror("Project error", "Could not locate ENGINE directory.")
            return "", ""
        ctrl_dir = os.path.join(engine_dir, "custom_controllers")
        os.makedirs(ctrl_dir, exist_ok=True)
        hpp = os.path.join(ctrl_dir, f"{base_name}.hpp")
        cpp = os.path.join(ctrl_dir, f"{base_name}.cpp")
        if not os.path.exists(hpp) or not os.path.exists(cpp):
            class_name = self._class_name_from_base(base_name)
            hpp_src = (
                f"#pragma once\n#include <string>\n\n"
                f"class {class_name} {{\npublic:\n"
                f"    std::string key = \"{base_name}\";\n\n"
                f"    {class_name}();\n"
                f"    void update(float dt);\n\n"
                f"private:\n    // TODO: add state here\n}};\n"
            )
            cpp_src = (
                f"#include \"{base_name}.hpp\"\n\n"
                f"{class_name}::{class_name}() {{\n    // TODO: initialize state\n}}\n\n"
                f"void {class_name}::update(float dt) {{\n    (void)dt;\n    // TODO: per-frame logic\n}}\n"
            )
            try:
                with open(hpp, "w", encoding="utf-8") as fh:
                    fh.write(hpp_src)
                with open(cpp, "w", encoding="utf-8") as fc:
                    fc.write(cpp_src)
            except Exception as e:
                messagebox.showerror("Write error", f"Failed to write controller files:\n{e}")
                return "", ""
        # ensure includes in engine sources
        include_line = f"#include \"custom_controllers/{base_name}.hpp\"\n"
        try:
            for rel in [
                os.path.join("ENGINE", "asset_info_methods", "animation_loader.cpp"),
                os.path.join("ENGINE", "asset_info_methods", "animation_loader.hpp"),
                os.path.join("ENGINE", "ui", "asset_info_ui.cpp"),
            ]:
                p = os.path.abspath(rel)
                if not os.path.exists(p):
                    continue
                with open(p, "r", encoding="utf-8") as f:
                    txt = f.read()
                if include_line.strip() in txt:
                    continue
                lines = txt.splitlines(True)
                ins = 0
                for i, ln in enumerate(lines[:100]):
                    if ln.lstrip().startswith('#include'):
                        ins = i + 1
                lines.insert(ins, include_line)
                with open(p, "w", encoding="utf-8") as f:
                    f.write("".join(lines))
        except Exception:
            pass
        return hpp, cpp

    @staticmethod
    def _class_name_from_base(base: str) -> str:
        parts = [p for p in base.replace('-', '_').split('_') if p]
        return ''.join(s[:1].upper() + s[1:] for s in parts)
