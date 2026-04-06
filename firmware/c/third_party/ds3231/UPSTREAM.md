DS3231 Upstream

- Original upstream: https://github.com/alpertng02/pico-ds3231
- Fork (source of truth for inki): https://github.com/c0de111/pico-ds3231
- Imported into this repo via git subtree (squashed history).

Current pin
- Tag in fork: v0.1.0-inki1 (examples OFF by default, temp fix, clear_alarm helpers)
- Upstream commit SHA recorded in merge message:
  - To read: `git log -1 --pretty=fuller -- firmware/c/third_party/ds3231`
  - Look for the line: `Merge commit '<sha>'` — this `<sha>` is the pinned upstream commit.

Manual version check
- Show the last subtree import commit message:
  - `git log -1 --pretty=%B -- firmware/c/third_party/ds3231`
- Extract the upstream commit SHA from the message:
  - `git log -1 --pretty=%B -- firmware/c/third_party/ds3231 | sed -n "s/^\s*Merge commit '\([0-9a-f]\+\)'.*/\1/p"`

Update policy (manual)
- Prefer consuming from the fork by commit SHA (not by tag) to avoid polluting inki’s tag namespace.
- Resolve a new SHA from the fork:
  - `git ls-remote pico-ds3231-fork refs/tags/<new-tag>^{} | awk '{print $1}'`
- Import the update:
  - `git subtree pull --prefix firmware/c/third_party/ds3231 pico-ds3231-fork <SHA> --squash`

Notes
- Do not edit files under this subtree unless absolutely necessary; contribute fixes to the fork instead, then pull the new SHA.
- The subtree may contain upstream example files; our build only uses `libraries/ds3231` from the fork (examples are OFF by default in the fork’s CMake).

