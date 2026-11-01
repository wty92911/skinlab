from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from skinlab.startup import run

try:
    run()
except Exception as error:
    import tkinter as tk
    from tkinter import messagebox
    root=tk.Tk(); root.withdraw()
    messagebox.showerror("SkinLab 启动失败",str(error),parent=root)
    root.destroy()
