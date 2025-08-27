# === File: blue_button.py ===
import tkinter as tk

class BlueButton(tk.Button):
    def __init__(self, parent, text, command=None, x=0, y=0, **kwargs):
        self.button = super().__init__(
            parent,
            text=text,
            command=command,
            bg="#007BFF",
            fg="white",
            activebackground="#0056b3",
            activeforeground="white",
            font=("Segoe UI", 12, "bold"),
            bd=0,
            highlightthickness=0,
            **kwargs
        )
        self.place_button(x, y)

    def place_button(self, x, y):
        if x == 0 and y == 0:
            self.place(relx=0.5, rely=1.0, anchor="s", y=-16)
        else:
            self.place(relx=x / 100.0, rely=y / 100.0, anchor="center")

    def state(self, states):
        self.config(state=states[0] if states else 'normal')
