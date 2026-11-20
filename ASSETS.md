# Assets

Asset binaries are **not in git**. This repo keeps only small text/config assets (`*.json`, `*.txt`, shaders, and the scripts beside them). Models, textures, audio, fonts, and Blender files live on a shared folder on the build PC.

`assets.manifest` lists every shared binary (relative path, size, SHA256). Commit that file when assets change. Never commit the binaries themselves.

## How to get the assets

From the repo root, on a machine that can see the share:

```
powershell -File tools\assets-sync.ps1 pull
```

The default share is `\\HOME-SERVER\u11-assets`. To use another path:

```
powershell -File tools\assets-sync.ps1 pull -SharePath \\HOME-SERVER\u11-assets
```

You can also copy files from the share into the local `assets\` folder by hand, keeping the same relative paths (share root = `assets\`).

`pull` copies missing or changed files to match the manifest. It never deletes local files.

## How to add or change an asset

1. Put the new or updated file in the local `assets\` folder.
2. Run `powershell -File tools\assets-sync.ps1 push`.
3. Commit the updated `assets.manifest` (and any text/config asset). Do not commit binaries.

`push` copies new or changed local files to the share and updates `assets.manifest`. It never deletes anything on the share. If it replaces a share file with a newer local version, it keeps the previous share file as `*.prev-<timestamp>`. It will not overwrite a share file with an older or identical copy.

## Verify

```
powershell -File tools\assets-sync.ps1 verify
```

Reports missing files and SHA256 mismatches between the local `assets\` folder, the share, and `assets.manifest`.

## Rules for helpers

- Never commit asset binaries or enable Git LFS for them.
- Do not delete files from the share.
- Do not move or delete files in the game's `assets\` folder just to "clean up" git.
- After a clone or when binaries are missing, run `pull` (or copy from the share) before building or testing.
- After changing a binary, run `push` and commit only `assets.manifest`.
