"""Tkinter GUI for the LTC6804 telemetry parser."""

import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from tkinterdnd2 import DND_FILES, TkinterDnD

from parse_telemetry import parse_file
from version import APP_NAME, VERSION


class TelemetryParserGUI:
    def __init__(self, root: TkinterDnD.Tk):
        self.root = root
        self.root.title(f"{APP_NAME} {VERSION}")
        self.root.geometry("1200x800")
        self.root.minsize(600, 400)

        self.parsed_output = ""

        self._build_ui()
        self._setup_dnd()

    def _build_ui(self):
        # Top frame with buttons
        top_frame = ttk.Frame(self.root)
        top_frame.pack(fill=tk.X, padx=10, pady=(10, 5))

        self.browse_btn = ttk.Button(
            top_frame, text="Browse CSV...", command=self._browse_file
        )
        self.browse_btn.pack(side=tk.LEFT)

        self.save_btn = ttk.Button(
            top_frame, text="Save As...", command=self._save_as, state=tk.DISABLED
        )
        self.save_btn.pack(side=tk.LEFT, padx=(10, 0))

        self.file_var = tk.StringVar(value="No file loaded")
        file_entry = ttk.Entry(top_frame, textvariable=self.file_var, state="readonly")
        file_entry.pack(side=tk.LEFT, padx=(15, 0), fill=tk.X, expand=True)

        # Drop hint label
        self.drop_hint = ttk.Label(
            self.root,
            text="Drop a CSV file here or use Browse",
            foreground="gray",
        )
        self.drop_hint.pack(pady=(0, 5))

        # Text area with ttk scrollbar
        text_frame = ttk.Frame(self.root)
        text_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))

        self.text_area = tk.Text(
            text_frame, wrap=tk.WORD, font=("Courier", 10), state=tk.DISABLED
        )
        scrollbar = ttk.Scrollbar(text_frame, orient=tk.VERTICAL, command=self.text_area.yview)
        self.text_area.configure(yscrollcommand=scrollbar.set)

        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.text_area.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    def _setup_dnd(self):
        self.text_area.drop_target_register(DND_FILES)
        self.text_area.dnd_bind("<<Drop>>", self._on_drop)

    def _on_drop(self, event):
        path = event.data.strip()
        # tkinterdnd2 may wrap path in braces on some platforms
        if path.startswith("{") and path.endswith("}"):
            path = path[1:-1]
        self._load_file(path)

    def _browse_file(self):
        path = filedialog.askopenfilename(
            parent=self.root,
            title="Select telemetry CSV",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if path:
            self._load_file(path)

    def _load_file(self, path: str):
        try:
            self.parsed_output = parse_file(path)
        except Exception as e:
            messagebox.showerror("Error", f"Failed to parse file:\n{e}")
            return

        self.file_var.set(path)
        self.drop_hint.pack_forget()

        self.text_area.config(state=tk.NORMAL)
        self.text_area.delete("1.0", tk.END)
        self.text_area.insert(tk.END, self.parsed_output)
        self.text_area.config(state=tk.DISABLED)

        self.save_btn.config(state=tk.NORMAL)

    def _save_as(self):
        path = filedialog.asksaveasfilename(
            parent=self.root,
            title="Save parsed output",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.parsed_output)
            messagebox.showinfo("Saved", f"Output saved to:\n{path}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save file:\n{e}")
