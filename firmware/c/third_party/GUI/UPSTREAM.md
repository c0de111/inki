GUI Paint Library Upstream

- Upstream repository: https://github.com/waveshareteam/e-Paper
- Integration method: git subtree (squashed history) under `firmware/c/third_party/GUI`
- Current pinned commit: TBD (latest working 4Gray implementation)

Manual version check
- Current vendored version (from header):
  - `grep -E "This version.*V[0-9]" firmware/c/third_party/GUI/GUI_Paint.h`
- Latest upstream commit (no local setup required):
  - `git ls-remote --heads https://github.com/waveshareteam/e-Paper.git | grep refs/heads/master`

Optional convenience remote
- One-time:
  - `git remote add e-paper https://github.com/waveshareteam/e-Paper.git`
  - `git fetch e-paper`
- Check newest commit:
  - `git ls-remote e-paper refs/heads/master`

Update policy (manual only)
- Update on demand to a chosen commit; nothing updates automatically.
- Commands to update subtree to a newer commit (from repo root):
  - `git fetch e-paper`
  - `git subtree pull --prefix firmware/c/third_party/GUI e-paper master --squash`

Verification checklist after update
- Build both configurations:
  - `./firmware/c/build.sh` (default) and `./firmware/c/build.sh --historian` and `./firmware/c/build.sh --weathermap`
- Quick on-device smoke test for Paint library functions (especially 4Gray mode).

Notes
- Only `GUI_Paint.c` is compiled by CMake; extra files are not built.
- This file is a local addition and persists across subtree pulls (will merge like any other change).
- Source path: RaspberryPi_JetsonNano/c/lib/GUI/ from upstream repository

Quick check for updates (copy/paste)

1) Check current vendored version from header:

```bash
grep -E "This version.*V[0-9]" firmware/c/third_party/GUI/GUI_Paint.h
```

2) Get latest upstream commit (no setup required):

```bash
git ls-remote --heads https://github.com/waveshareteam/e-Paper.git | grep refs/heads/master
```

3) Suggested update command (if newer exists):

```bash
# One-time if not added yet:
git remote add e-paper https://github.com/waveshareteam/e-Paper.git || true
git fetch e-paper

# Then pull the latest commit into the subtree
git subtree pull --prefix firmware/c/third_party/GUI e-paper master --squash
```

Optional helper script (create at `scripts/check_gui.sh`):

```bash
#!/usr/bin/env bash
set -euo pipefail

CUR_VER=$(grep -E "This version.*V[0-9]" firmware/c/third_party/GUI/GUI_Paint.h | head -1)
LATEST_COMMIT=$(git ls-remote --heads https://github.com/waveshareteam/e-Paper.git | grep refs/heads/master | cut -f1)

echo "Current: $CUR_VER"
echo "Latest commit: $LATEST_COMMIT"

echo
echo "To update:" 
echo "  git remote add e-paper https://github.com/waveshareteam/e-Paper.git || true"
echo "  git fetch e-paper"
echo "  git subtree pull --prefix firmware/c/third_party/GUI e-paper master --squash"
```