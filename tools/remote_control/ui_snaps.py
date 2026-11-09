"""Snap every tile and settings page of a unit, and compare two sets of snaps: a check that a change left the screens
as they were (ADR-012).

Usage, from the repo root with the repo venv:
    python tools/remote_control/ui_snaps.py capture --port COM11 logs/rc/snaps/before
    python tools/remote_control/ui_snaps.py capture --host owmc-83a124.local logs/rc/snaps/before   (over Wi-Fi)
    python tools/remote_control/ui_snaps.py compare logs/rc/snaps/before logs/rc/snaps/after [montage.png]

capture: drives the unit over its USB console (rc_run.Session): the four tiles (timer, level, dope, settings) and
the nine settings pages, in the unit's rotation and the next one. `rot` is RAM only; the rotation and the level tile
are put back at the end. Nothing is tapped, so no setting changes.

compare: per screen, the share of pixels that differ and the box around them. Live values (the level's angle, the
Power page's readings, the version on About, the settings hub's scroll position) differ by nature; anything else is
worth a look. With a montage path, the screens that differ are saved side by side (before, after, difference).
"""
import argparse
import pathlib
import re
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rc_run  # noqa: E402

TILES = {"timer": "2 0", "level": "2 1", "dope": "2 2", "settings": "3 1"}
PAGES = ["Timer", "Dope", "Kestrel", "Level", "Display", "Wi-Fi", "Power", "Advanced", "About"]


def capture(port: str, host, out: pathlib.Path) -> int:
    out.mkdir(parents=True, exist_ok=True)
    session = rc_run.Session(port, out / "capture.log", host=host)
    failures = []
    try:
        if not session.wait_ready():
            print("unit not ready")
            return 1
        own = int(re.search(r"rot=(\d)", session.command("state")).group(1))
        for rot in (own, (own + 1) % 4):
            session.command(f"rot {rot}")
            time.sleep(1.0)
            for name, tile in TILES.items():
                session.command("settings home")
                session.command(f"tile {tile}")
                time.sleep(0.8)
                if not session.snap(out / f"r{rot}_tile_{name}.png"):
                    failures.append(f"r{rot} tile {name}")
            session.command("tile 3 1")
            for page in PAGES:
                session.command("settings home")
                reply = session.command(f"settings open {page}")
                time.sleep(0.6)
                if not reply.startswith("@ok") or not session.snap(out / f"r{rot}_page_{page.replace('-', '')}.png"):
                    failures.append(f"r{rot} page {page}: {reply}")
        session.command("settings home")
        session.command(f"rot {own}")
        session.command("tile 2 1")
    finally:
        session.close()
    print("failures:", failures or "none")
    print("snaps:", len(list(out.glob("*.png"))), "in", out)
    return 1 if failures else 0


def compare(before: pathlib.Path, after: pathlib.Path, montage_path) -> int:
    from PIL import Image, ImageChops

    rows = []
    for a in sorted(before.glob("*.png")):
        b = after / a.name
        if not b.exists():
            print(f"{a.name:32s} missing after")
            continue
        ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
        if ia.size != ib.size:
            print(f"{a.name:32s} size {ia.size} -> {ib.size}")
            continue
        diff = ImageChops.difference(ia, ib).convert("L").point(lambda v: 255 if v > 24 else 0)
        changed = diff.histogram()[255]
        share = changed / (ia.size[0] * ia.size[1])
        print(f"{a.name:32s} {share * 100:6.2f}%  box={diff.getbbox()}")
        if changed:
            rows.append((ia, ib, diff.convert("RGB")))
    if montage_path and rows:
        w = max(r[0].size[0] for r in rows)
        h = sum(r[0].size[1] + 4 for r in rows)
        montage = Image.new("RGB", (w * 3 + 20, h), "white")
        y = 0
        for ia, ib, d in rows:
            montage.paste(ia, (0, y))
            montage.paste(ib, (w + 10, y))
            montage.paste(d, (2 * w + 20, y))
            y += ia.size[1] + 4
        montage.save(montage_path)
        print("montage:", montage_path)
    return 0


def main(argv) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="action", required=True)
    cap = sub.add_parser("capture", help="snap every tile and settings page")
    cap.add_argument("--port", default=rc_run.DEFAULT_PORT)
    cap.add_argument("--host", default=None, help="the unit's address or mDNS name, over Wi-Fi instead of --port")
    cap.add_argument("out", type=pathlib.Path)
    cmp_ = sub.add_parser("compare", help="compare two capture folders")
    cmp_.add_argument("before", type=pathlib.Path)
    cmp_.add_argument("after", type=pathlib.Path)
    cmp_.add_argument("montage", type=pathlib.Path, nargs="?")
    args = parser.parse_args(argv)
    if args.action == "capture":
        return capture(args.port, args.host, args.out)
    return compare(args.before, args.after, args.montage)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
