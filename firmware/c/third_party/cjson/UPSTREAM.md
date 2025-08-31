cJSON Upstream

- Upstream repository: https://github.com/DaveGamble/cJSON
- Current integration: manual vendored copy in `third_party/cjson/` (no submodule/subtree yet)
- Current vendored version (from header):
  - Check with: `grep -E "CJSON_VERSION_(MAJOR|MINOR|PATCH)" third_party/cjson/cJSON.h`

Manual check for new releases
- Without any local setup:
  - `git ls-remote --tags --sort='v:refname' https://github.com/DaveGamble/cJSON.git | tail -n1`
- Or add a convenience remote (one-time) and check tags:
  - `git remote add cjson https://github.com/DaveGamble/cJSON.git`
  - `git fetch cjson --tags`
  - `git -c versionsort.suffix=- ls-remote --tags --sort='v:refname' cjson | tail -n1`

Update options (choose one when you decide to upgrade)
1) Manual replacement (current method)
   - Download the chosen release (e.g., from GitHub tag).
   - Overwrite `third_party/cjson/cJSON.c` and `third_party/cjson/cJSON.h`.
   - Build (`./build.sh`) and commit.

2) Optional: migrate to git subtree (enables easy on-demand pulls)
   - `git remote add cjson https://github.com/DaveGamble/cJSON.git`
   - `git fetch cjson --tags`
   - `git rm -r third_party/cjson && git commit -m "chore: prepare cJSON subtree import"`
   - `git subtree add --prefix third_party/cjson cjson vX.Y.Z --squash`
   - Verify build, then future updates via:
     - `git fetch cjson --tags`
     - `git subtree pull --prefix third_party/cjson cjson vX.Y.Z+1 --squash`

Verification checklist after update
- Build both use cases (`./build.sh`, `./build.sh --historian`) without errors.
- Run quick on-device checks for any JSON parsing/serialization paths that matter.

