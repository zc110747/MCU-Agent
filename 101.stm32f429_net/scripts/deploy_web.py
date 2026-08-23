#!/usr/bin/env python3
# deploy_web.py - 把 web/dist/ 部署到 SD 卡的 web/ 目录
#
# 用法:
#   python scripts/deploy_web.py            # 自动探测 SD 卡盘符
#   python scripts/deploy_web.py E:         # 指定盘符
#
# 固件期望路径: 0:/web/index.html  (即 SD 根目录下 web/index.html)
# 因此把 web/dist/* 拷贝到 <SD>:/web/ 下
import os
import sys
import shutil

PROJ_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DIST = os.path.join(PROJ_ROOT, "web", "dist")


def find_sd_root():
    """探测含 web/ 或为空 FAT 的可移动盘。返回盘符根路径或 None。"""
    candidates = []
    if sys.platform.startswith("win"):
        import string
        for d in string.ascii_uppercase:
            root = f"{d}:/"
            if os.path.isdir(root):
                # 尝试判断是否为可移动/有写权限
                try:
                    test = os.path.join(root, ".deploy_test")
                    with open(test, "w") as f:
                        f.write("x")
                    os.remove(test)
                    candidates.append(root)
                except Exception:
                    pass
    else:
        for m in ("/media", "/mnt", "/Volumes"):
            if os.path.isdir(m):
                for name in os.listdir(m):
                    candidates.append(os.path.join(m, name))
    return candidates


def main():
    if not os.path.isdir(DIST):
        print(f"ERROR: {DIST} 不存在，请先构建 Vue 项目 (cd web && npm run build)")
        sys.exit(1)

    if len(sys.argv) > 1:
        sd_root = sys.argv[1]
        if not sd_root.endswith("/") and not sd_root.endswith("\\"):
            sd_root += "/"
    else:
        cands = find_sd_root()
        if not cands:
            print("ERROR: 未自动探测到 SD 卡，请手动指定盘符，如: python deploy_web.py E:")
            sys.exit(1)
        # 优先选择已含 web/ 的盘
        sd_root = None
        for c in cands:
            if os.path.isdir(os.path.join(c, "web")):
                sd_root = c
                break
        if sd_root is None:
            # 列出来让用户选
            print("探测到以下可写盘，请选择 SD 卡对应盘符:")
            for i, c in enumerate(cands):
                print(f"  [{i}] {c}")
            try:
                idx = int(input("输入序号: ").strip())
                sd_root = cands[idx]
            except Exception:
                print("无效输入")
                sys.exit(1)

    dest = os.path.join(sd_root, "web")
    print(f"部署: {DIST}  ->  {dest}")
    os.makedirs(dest, exist_ok=True)

    # 拷贝 dist 下所有内容 (index.html + assets/)
    count = 0
    for entry in os.listdir(DIST):
        src = os.path.join(DIST, entry)
        dst = os.path.join(dest, entry)
        if os.path.isdir(src):
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)
        count += 1
        print(f"  + {entry}")

    # 校验
    idx = os.path.join(dest, "index.html")
    if os.path.isfile(idx):
        print(f"\nOK: 部署完成，SD 卡 {idx} 存在 ({count} 项)")
        print("提示: 把 SD 卡插回板子，重新上电，访问 http://192.168.10.99/ 应显示 Vue 控制台")
    else:
        print(f"\nERROR: 部署后未找到 {idx}")
        sys.exit(1)


if __name__ == "__main__":
    main()
