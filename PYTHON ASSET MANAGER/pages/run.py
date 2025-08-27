import os
import subprocess
import tkinter as tk
from tkinter import ttk

def launch_engine():
    selected = listbox.curselection()
    if not selected:
        return
    selected_map = listbox.get(selected[0])
    map_path = os.path.join("MAPS", selected_map)
    executable_path = os.path.join("engine", "Release", "engine.exe")
    subprocess.run([executable_path, map_path])

root = tk.Tk()
root.title("Select a Map to Run")

frame = ttk.Frame(root, padding=20)
frame.pack(fill=tk.BOTH, expand=True)

ttk.Label(frame, text="Available Maps:", font=("Segoe UI", 12, "bold")).pack(anchor="w")

listbox = tk.Listbox(frame, height=15, width=40)
listbox.pack(fill=tk.BOTH, expand=True, pady=(5, 10))

map_dirs = [d for d in os.listdir("MAPS") if os.path.isdir(os.path.join("MAPS", d))]
for name in sorted(map_dirs):
    listbox.insert(tk.END, name)

run_button = ttk.Button(frame, text="Run", command=launch_engine)
run_button.pack(pady=(10, 0))

root.mainloop()
