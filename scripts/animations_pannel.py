
from __future__ import annotations
from typing import Any, Dict, List, Optional, Callable, Set
import os
import tkinter as tk
from tkinter import ttk

from movement_modal import MovementModal
from sources_element_pannel import SourcesElementPanel


class AnimationsPanel:
    def __init__(
        self,
        parent: tk.Widget,
        anim_id: str,
        payload: Dict[str, Any],
        *,
        on_changed: Optional[Callable[[str, Dict[str, Any]], None]] = None,
        on_renamed: Optional[Callable[[str, str], None]] = None,
        on_delete: Optional[Callable[[str], None]] = None,
        preview_provider: Any = None,
        asset_folder: Optional[str] = None,
        list_animation_names: Optional[Callable[[], List[str]]] = None,
        resolve_animation_payload: Optional[Callable[[str], Optional[Dict[str, Any]]]] = None,
    ) -> None:
        self.parent = parent
        self.node_id = str(anim_id)
        self.on_changed = on_changed
        self.on_renamed = on_renamed
        self.on_delete = on_delete
        self.preview_provider = preview_provider
        self.asset_folder = asset_folder
        self.list_animation_names = list_animation_names or (lambda: [])
        self.resolve_animation_payload = resolve_animation_payload

        self.payload = self._coerce_payload(self.node_id, payload)
        self._sync_frames_from_source()

        
        self.frame = tk.LabelFrame(parent, text=self.node_id, bd=6, relief=tk.GROOVE)
        self._build_ui()
        self._refresh_preview()

    
    def _build_ui(self) -> None:
        header = ttk.Frame(self.frame)
        header.grid(row=0, column=0, sticky="ew", padx=8, pady=(6, 8))
        self.frame.columnconfigure(0, weight=1)

        id_font = ("Segoe UI", 14, "bold")
        ttk.Label(header, text="ID:", font=id_font).pack(side="left")
        self.id_var = tk.StringVar(value=self.node_id)
        
        id_entry = tk.Entry(header, textvariable=self.id_var, width=24, font=id_font)
        id_entry.pack(side="left", padx=(4, 10))
        id_entry.bind("<FocusOut>", self._commit_rename)
        id_entry.bind("<Return>", self._commit_rename)

        ttk.Button(header, text="Delete", command=self._do_delete).pack(side="right")

        self._preview_label = tk.Label(header, bd=0, highlightthickness=0, background="#2d3436")
        self._preview_label.pack(side="right", padx=6)
        self._preview_img = None

        body = ttk.Frame(self.frame)
        body.grid(row=1, column=0, sticky="nsew", padx=8, pady=(0, 8))
        self.frame.rowconfigure(1, weight=1)

        
        self.sources_panel = SourcesElementPanel(
            body,
            self.payload.get("source", {}),
            on_changed=self._apply_changes,
            asset_folder=self.asset_folder,
            get_current_name=lambda: self.node_id,
            list_animation_names=self.list_animation_names,
        )
        self.sources_panel.get_frame().pack(fill="x", pady=4)

        
        pf = ttk.LabelFrame(body, text="Playback")
        pf.pack(fill="x", pady=4)
        self.flipped_var = tk.BooleanVar(value=bool(self.payload.get("flipped_source", False)))
        self.reversed_var = tk.BooleanVar(value=bool(self.payload.get("reverse_source", False)))
        self.locked_var = tk.BooleanVar(value=bool(self.payload.get("locked", False)))
        self.loop_var = tk.BooleanVar(value=bool(self.payload.get("loop", False)))  
        
        self.rnd_start_var = tk.BooleanVar(value=bool(self.payload.get("rnd_start", False)))

        ttk.Checkbutton(pf, text="flipped",  variable=self.flipped_var,  command=self._apply_changes).grid(row=0, column=0, sticky="w")
        ttk.Checkbutton(pf, text="reverse",  variable=self.reversed_var, command=self._apply_changes).grid(row=0, column=1, sticky="w")
        ttk.Checkbutton(pf, text="locked",   variable=self.locked_var,   command=self._apply_changes).grid(row=0, column=2, sticky="w")
        ttk.Checkbutton(pf, text="rnd start",variable=self.rnd_start_var,command=self._apply_changes).grid(row=0, column=3, sticky="w")
        ttk.Checkbutton(pf, text="loop",     variable=self.loop_var,     command=self._apply_changes).grid(row=0, column=4, sticky="w")  

        ttk.Label(pf, text="speed").grid(row=1, column=0, sticky="e")
        self.speed_var = tk.IntVar(value=int(self.payload.get("speed_factor", 1)))
        spin_speed = ttk.Spinbox(pf, from_=-20, to=20, textvariable=self.speed_var, width=8, command=self._apply_changes)
        spin_speed.grid(row=1, column=1, sticky="w", padx=4)
        spin_speed.bind("<FocusOut>", lambda _e: self._apply_changes())

        
        ttk.Label(pf, text="frames").grid(row=2, column=0, sticky="e")
        self.frames_var = tk.IntVar(value=int(self.payload.get("number_of_frames", 1)))
        frames_entry = ttk.Entry(pf, textvariable=self.frames_var, width=8, state="readonly")
        frames_entry.grid(row=2, column=1, sticky="w", padx=4)

        
        mvf = ttk.LabelFrame(body, text="Movement")
        mvf.pack(fill="x", pady=4)
        ttk.Label(mvf, text="Edit per-frame movement vectors").grid(row=0, column=0, sticky="w", padx=4)
        ttk.Button(mvf, text="Edit Movement...", command=self._open_movement_modal).grid(row=0, column=1, sticky="e", padx=4)

        
        oef = ttk.LabelFrame(body, text="On End")
        oef.pack(fill="x", pady=4)
        self.on_end_var = tk.StringVar(value=str(self.payload.get("on_end", "default") or "default"))
        self.on_end_combo = ttk.Combobox(oef, textvariable=self.on_end_var, state="readonly", width=36)
        self.on_end_combo.pack(side="left", padx=6, pady=4)
        self.on_end_combo.bind("<<ComboboxSelected>>", lambda _e: self._apply_changes())
        self._refresh_on_end_options()

    
    def get_frame(self) -> ttk.Frame:
        return self.frame

    
    def _count_frame_files(self, path: str) -> int:
        """Count frame files in a folder, excluding GIFs and non-files."""
        if not path:
            return 1
        try:
            count = 0
            with os.scandir(path) as it:
                for entry in it:
                    if not entry.is_file():
                        continue
                    name = entry.name.lower()
                    
                    if name.endswith((".png", ".jpg", ".jpeg", ".bmp", ".webp")):
                        count += 1
            return max(1, count)
        except Exception:
            return 1

    def _compute_frames_from_source(self, src: Dict[str, Any]) -> int:
        """Compute number_of_frames by following source chains until a real frame source is found."""
        visited: Set[str] = set()
        current = dict(src or {})

        while True:
            kind = (current or {}).get("kind", "folder")

            if kind == "folder":
                base = self.asset_folder or ""
                rel = (current.get("path") or "").strip()
                path = os.path.join(base, rel) if rel else base
                return self._count_frame_files(path)

            if kind == "spritesheet":
                
                cols = int((current.get("cols") or 0))
                rows = int((current.get("rows") or 0))
                frames = int((current.get("frames") or 0))
                if cols > 0 and rows > 0:
                    return max(1, cols * rows)
                if frames > 0:
                    return frames
                
                return 1

            if kind == "animation":
                
                
                target = (current.get("name") or current.get("path") or "").strip()
                if not target:
                    return 1
                
                if target in visited:
                    return 1
                visited.add(target)

                
                resolved_payload: Optional[Dict[str, Any]] = None
                if callable(self.resolve_animation_payload):
                    try:
                        resolved_payload = self.resolve_animation_payload(target)
                    except Exception:
                        resolved_payload = None

                if not isinstance(resolved_payload, dict):
                    
                    return 1

                
                current = dict(resolved_payload.get("source") or {})
                continue

            
            return 1

    def _sync_frames_from_source(self) -> None:
        n = self._compute_frames_from_source(self.payload.get("source", {}))
        self.payload["number_of_frames"] = n
        
        if hasattr(self, "frames_var"):
            self.frames_var.set(n)

    def set_payload(self, new_payload: Dict[str, Any]) -> None:
        self.payload = self._coerce_payload(self.node_id, new_payload)
        self._sync_frames_from_source()
        
        self.sources_panel.set_values(self.payload.get("source", {}))

        self.flipped_var.set(bool(self.payload.get("flipped_source", False)))
        self.reversed_var.set(bool(self.payload.get("reverse_source", False)))
        self.locked_var.set(bool(self.payload.get("locked", False)))
        self.loop_var.set(bool(self.payload.get("loop", False)))
        try:
            self.rnd_start_var.set(bool(self.payload.get("rnd_start", False)))
        except Exception:
            pass
        self.speed_var.set(int(self.payload.get("speed_factor", 1)))
        self.frames_var.set(int(self.payload.get("number_of_frames", 1)))
        self.on_end_var.set(str(self.payload.get("on_end", "default") or "default"))
        self._refresh_on_end_options()
        self._refresh_preview()
        if self.on_changed:
            self.on_changed(self.node_id, self.payload)

    
    def _commit_rename(self, _evt=None):
        new_id = self.id_var.get().strip()
        if not new_id or new_id == self.node_id:
            self.id_var.set(self.node_id)
            return
        old = self.node_id
        self.node_id = new_id
        try:
            self.frame.configure(text=new_id)
        except Exception:
            pass
        
        try:
            self._refresh_on_end_options()
        except Exception:
            pass
        if self.on_renamed:
            self.on_renamed(old, new_id)

    def _do_delete(self):
        if self.on_delete:
            self.on_delete(self.node_id)

    def _open_movement_modal(self):
        parent = self.frame.winfo_toplevel()
        n = self._compute_frames_from_source(self.payload.get("source", {}))
        MovementModal(
            parent=parent,
            movement=self.payload.get("movement", []),
            frames_count=n,
            on_save=self._on_movement_saved,
            title=f"Movement: {self.node_id}",
        )

    def _on_movement_saved(self, new_movement: List[List[int]]):
        self.payload["movement"] = new_movement

        
        tot_dx = sum(int(it[0]) for it in new_movement)  
        tot_dy = sum(int(it[1]) for it in new_movement)
        self.payload["movement_total"] = {"dx": int(tot_dx), "dy": int(tot_dy)}

        if self.on_changed:
            self.on_changed(self.node_id, self.payload)
        self._refresh_preview()

    def _apply_changes(self):
        payload = self.payload
        payload["source"] = self.sources_panel.read_values()
        payload["flipped_source"] = bool(self.flipped_var.get())
        payload["reverse_source"] = bool(self.reversed_var.get())
        payload["locked"] = bool(self.locked_var.get())
        payload["loop"] = bool(self.loop_var.get())  
        payload["rnd_start"] = bool(self.rnd_start_var.get())

        v = int(self.speed_var.get())
        if v == 0:
            v = 1
        v = max(-20, min(20, v))
        payload["speed_factor"] = v
        payload["on_end"] = str(self.on_end_var.get() or "default")

        
        payload["number_of_frames"] = self._compute_frames_from_source(payload.get("source", {}))
        self.frames_var.set(payload["number_of_frames"])

        
        mv = payload.get("movement", [])
        if not isinstance(mv, list):
            mv = []
        n = payload["number_of_frames"]
        if len(mv) < n:
            mv.extend([[0, 0] for _ in range(n - len(mv))])
        elif len(mv) > n:
            mv = mv[:n]
        if n >= 1:
            mv[0] = [0, 0]
        payload["movement"] = mv

        if self.on_changed:
            self.on_changed(self.node_id, payload)
        self._refresh_preview()

    def _refresh_preview(self):
        provider = self.preview_provider
        if not provider:
            return
        try:
            img = provider.get_preview(self.node_id, self.payload)
            self._preview_img = img
            if img is not None:
                self._preview_label.configure(image=img)
        except Exception:
            pass

    
    @staticmethod
    def _coerce_payload(anim_name: str, p: Dict[str, Any]) -> Dict[str, Any]:
        p = dict(p or {})
        src = dict(p.get("source") or {})
        _kind = src.get("kind", "folder")
        p["source"] = {
            "kind": _kind,
            "path": src.get("path", anim_name if _kind == "folder" else ""),
            "name": src.get("name", None if _kind == "folder" else ""),
        }
        p.setdefault("flipped_source", False)
        p.setdefault("reverse_source", False)
        p.setdefault("locked", False)
        p.setdefault("loop", bool(p.get("loop", False)))  
        p.setdefault("rnd_start", bool(p.get("rnd_start", False)))
        try:
            _raw = int(str(p.get("speed_factor", 1)).strip())
        except Exception:
            _raw = 1
        if _raw == 0:
            _raw = 1
        _raw = max(-20, min(20, _raw))
        p.setdefault("speed_factor", _raw)

        
        try:
            p.setdefault("number_of_frames", max(1, int(p.get("number_of_frames", 1))))
        except Exception:
            p.setdefault("number_of_frames", 1)

        
        mv = p.get("movement")
        n = p["number_of_frames"]
        if not isinstance(mv, list) or len(mv) < 1:
            mv = [[0, 0] for _ in range(n)]
        if len(mv) != n:
            if len(mv) < n:
                mv.extend([[0, 0] for _ in range(n - len(mv))])
            else:
                mv = mv[:n]
        mv[0] = [0, 0]
        p["movement"] = mv

        val = p.get("on_end")
        if not val:
            val = "default"
        p["on_end"] = str(val)

        
        return p

    def _refresh_on_end_options(self):
        try:
            names = sorted(self.list_animation_names())
        except Exception:
            names = []
        
        base = ["default", "reverse", "end"]
        
        seen = set()
        values = []
        for x in base + names:
            if x in seen:
                continue
            seen.add(x)
            values.append(x)
        try:
            self.on_end_combo["values"] = values
        except Exception:
            pass
        
        cur = str(self.on_end_var.get() or "default")
        if cur not in values:
            self.on_end_var.set("default")
