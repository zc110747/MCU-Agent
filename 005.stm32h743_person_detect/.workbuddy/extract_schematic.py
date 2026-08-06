import sys, os
import fitz  # pymupdf

pdf_path = r"E:\cnb\embeds\stm32h7_project\document\LXB743ZI-P1原理图.pdf"
out_dir = r"E:\workspace\stm32h7_person_detect_completed\.workbuddy\schematic"
os.makedirs(out_dir, exist_ok=True)

doc = fitz.open(pdf_path)
print("PAGES:", doc.page_count)

for i, page in enumerate(doc):
    txt = page.get_text()
    # save text
    with open(os.path.join(out_dir, f"page_{i+1}.txt"), "w", encoding="utf-8") as f:
        f.write(txt)
    # render to png at 150 dpi
    pix = page.get_pixmap(dpi=150)
    pix.save(os.path.join(out_dir, f"page_{i+1}.png"))
    # quick stats
    words = len(txt.split())
    print(f"page {i+1}: chars={len(txt)} words={words} size={pix.width}x{pix.height}")

print("DONE")
