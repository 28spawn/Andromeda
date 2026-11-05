"""Where baseline factory bundles live.

Every tool and test finds them through find().
"""
import os

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(FW)
SEARCH = [
    os.path.join(REPO, "local_storage", "custom-fw", "factory_bundles"),
    os.path.join(FW, "factory_bundles"),
    os.path.join(REPO, "factory_bundles"),
]


ALIASES = {
    "stock_am4_assets.bin": ["stock_am4_assets.bin", "sk17_am4_ampCab.bin"],
    "stock_am4_presets.bin": ["stock_am4_presets.bin", "sk17_am4_preset.bin"],
}

def find(name, required=True):
    """The full path of a bundle, or None (required=False) / SystemExit (required=True)."""
    cands = ALIASES.get(name, [name])
    for cand in cands:
        for d in SEARCH:
            p = os.path.join(d, cand)
            if os.path.isfile(p):
                return p
    if required:
        raise SystemExit(f"{name}: not found in " + " or ".join(os.path.relpath(d, REPO) for d in SEARCH))
    return None
