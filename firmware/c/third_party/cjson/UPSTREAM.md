cJSON Upstream

- Upstream repository: https://github.com/DaveGamble/cJSON
- Integration method: git subtree (squashed history) under `firmware/c/third_party/cjson`
- Current pinned tag: v1.7.18

Manual version check
- Current vendored version (from header):
  - `grep -E "CJSON_VERSION_(MAJOR|MINOR|PATCH)" firmware/c/third_party/cjson/cJSON.h`
- Latest upstream release tag (no local setup required):
  - `git ls-remote --tags --sort='v:refname' https://github.com/DaveGamble/cJSON.git | tail -n1`

Optional convenience remote
- One-time:
  - `git remote add cjson https://github.com/DaveGamble/cJSON.git`
  - `git fetch cjson --tags`
- Check newest tag:
  - `git -c versionsort.suffix=- ls-remote --tags --sort='v:refname' cjson | tail -n1`

Update policy (manual only)
- Update on demand to a chosen tag; nothing updates automatically.
- Commands to update subtree to a newer tag (from repo root):
  - `git fetch cjson --tags`
  - `git subtree pull --prefix firmware/c/third_party/cjson cjson vX.Y.Z --squash`

Verification checklist after update
- Build both configurations:
  - `./firmware/c/build.sh` (default) and `./firmware/c/build.sh --historian`
- Quick on-device smoke test for any JSON paths you rely on.

Notes
- Only `cJSON.c` is compiled by CMake; extra files (tests, CI configs) are not built.
- This file is a local addition and persists across subtree pulls (will merge like any other change).

Quick check for updates (copy/paste)

1) Compute current vendored version from header:

```bash
awk '
  /CJSON_VERSION_MAJOR/ {M=$3}
  /CJSON_VERSION_MINOR/ {m=$3}
  /CJSON_VERSION_PATCH/ {p=$3}
  END {print "v" M "." m "." p}
' firmware/c/third_party/cjson/cJSON.h
```

2) Get latest upstream release tag (no setup required):

```bash
git ls-remote --tags --sort='v:refname' https://github.com/DaveGamble/cJSON.git \
 | sed -n 's#.*/tags/##p' | sed 's/\^{}//' | tail -n1
```

3) Suggested update command (if newer exists):

```bash
# One-time if not added yet:
git remote add cjson https://github.com/DaveGamble/cJSON.git || true
git fetch cjson --tags

# Then pull the chosen tag into the subtree (replace TAG as needed)
git subtree pull --prefix firmware/c/third_party/cjson cjson TAG --squash
```

Optional helper script (create at `scripts/check_cjson.sh`):

```bash
#!/usr/bin/env bash
set -euo pipefail

CUR_TAG=$(awk '/CJSON_VERSION_MAJOR/{M=$3}/CJSON_VERSION_MINOR/{m=$3}/CJSON_VERSION_PATCH/{p=$3} END{print "v" M "." m "." p}' firmware/c/third_party/cjson/cJSON.h)
LATEST_TAG=$(git ls-remote --tags --sort='v:refname' https://github.com/DaveGamble/cJSON.git | sed -n 's#.*/tags/##p' | sed 's/\^{}//' | tail -n1)

echo "Current: $CUR_TAG"
echo "Latest : $LATEST_TAG"

if [ "$CUR_TAG" != "$LATEST_TAG" ]; then
  echo
  echo "To update:" 
  echo "  git remote add cjson https://github.com/DaveGamble/cJSON.git || true"
  echo "  git fetch cjson --tags"
  echo "  git subtree pull --prefix firmware/c/third_party/cjson cjson $LATEST_TAG --squash"
else
  echo "cJSON is up to date."
fi
```

