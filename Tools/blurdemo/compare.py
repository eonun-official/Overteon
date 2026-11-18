#!/usr/bin/env python3
"""Show one or two images in a Tk window, with a caption.

    ./compare.py grid.png "sigma 6  ..."
    ./compare.py a.png b.png "caption"      # side by side
"""
import os
import sys
import tkinter as tk
from PIL import Image, ImageTk


def main(argv):
    args = [a for a in argv[1:]]
    if not args:
        sys.exit(__doc__)

    images, caption = [], ""
    for a in args:
        if os.path.exists(a):
            images.append(a)
        else:
            caption = a
    if not images:
        sys.exit("no such image")

    pics = [Image.open(p).convert("RGB") for p in images]

    root = tk.Tk()
    root.title(os.path.basename(images[0]))
    root.configure(bg="#0b0d10")

    row = tk.Frame(root, bg="#0b0d10")
    row.pack()
    for path, im in zip(images, pics):
        box = tk.Frame(row, bg="#0b0d10")
        box.pack(side="left", padx=8, pady=8, anchor="n")
        tk.Label(box, text=f"{os.path.basename(path)}   {im.width}x{im.height}",
                 bg="#0b0d10", fg="#43d9c4", anchor="w",
                 font=("TkDefaultFont", 11, "bold")).pack(fill="x")
        lbl = tk.Label(box, bg="#0b0d10", bd=0)
        lbl.pack()
        photo = ImageTk.PhotoImage(im)
        lbl.configure(image=photo)
        lbl.image = photo

    if caption:
        tk.Label(root, text=caption, bg="#0b0d10", fg="#e7ebf2",
                 font=("TkFixedFont", 10), anchor="w").pack(fill="x", padx=8,
                                                            pady=(0, 8))
    root.mainloop()


if __name__ == "__main__":
    main(sys.argv)
