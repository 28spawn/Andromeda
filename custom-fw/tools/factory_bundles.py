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


def find(name, required=True):
    """The full path of a bundle, or None (required=False) / SystemExit (required=True)."""
    for d in SEARCH:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    if required:
        raise SystemExit(f"{name}: not found in " + " or ".join(os.path.relpath(d, REPO) for d in SEARCH))
    return None
