#!/usr/bin/env python3
"""notepad420 delivery.json 输入指纹与交付文件集校验。

用法:
  python build/delivery_fingerprint.py          # 依据 delivery.json 的 inputs 重算指纹并打印
  python build/delivery_fingerprint.py --check  # 与记录值比对,不一致返回 2

口径(确定性):
  inputs 按 delivery.json 声明顺序逐项展开;目录按相对路径(小写、正斜杠)排序。
  每项一行 "input\trelpath\tsha256" 喂给总 sha256;指纹即总哈希。
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DELIVERY = ROOT / ".target" / "delivery.json"


def iter_entries(inputs):
    for item in inputs:
        p = ROOT / item
        if p.is_file():
            yield item, item, p
        elif p.is_dir():
            for f in sorted(p.rglob("*"), key=lambda x: str(x).lower()):
                if f.is_file():
                    yield item, f.relative_to(ROOT).as_posix().lower(), f
        else:
            raise SystemExit(f"[ERROR] 指纹输入不存在: {item}")


def fingerprint(inputs):
    h = hashlib.sha256()
    for item, rel, path in iter_entries(inputs):
        h.update(f"{item}\t{rel}\t".encode("utf-8"))
        h.update(hashlib.sha256(path.read_bytes()).hexdigest().encode("ascii"))
        h.update(b"\n")
    return h.hexdigest()


def main():
    doc = json.loads(DELIVERY.read_text(encoding="utf-8"))
    inputs = doc["app"]["source"]["inputs"]
    value = fingerprint(inputs)
    if "--check" in sys.argv:
        recorded = doc["app"]["source"].get("value", "")
        if value != recorded:
            print(f"[STALE] 记录 {recorded[:12]}… != 当前 {value[:12]}…")
            return 2
        print(f"[FRESH] {value}")
        return 0
    print(value)
    return 0


if __name__ == "__main__":
    sys.exit(main())
