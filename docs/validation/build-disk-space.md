# Build disk space (27 October 2026)

Allow **20 GiB (about 22 GB) free after installing Xcode and the build tools**
for a clean build of one target with both games. The previous 40 GB requirement
was not supported by a measurement of this release.

## Measurements

Measured the published v0.2.0 source at
`43f83ba5d27c11d1c6eb16fb6afd08b6eb6cc345`, using PadMint main at
`0d0596303c3b0adf2b78f771f29def357ef608c4`:

| Target | Largest sampled workspace | Final footprint including packaging and output copy | Player check |
| --- | ---: | ---: | --- |
| iPhone/iPad | 12.76 GiB | 14.41 GiB | PASS |
| Mac | 12.28 GiB | 12.96 GiB | PASS |

The iPhone/iPad total includes 9.09 GiB in `generated/`, 2.53 GiB in
`ref/`, a 0.97 GiB temporary packaging copy, two approximately 0.68 GiB IPA
copies, and the checkout, Python environment, pip cache and build records.
Generated code and build intermediates account for most of the space.

## Method and scope

Ran these sequentially, with Apple's `/usr/bin/python3` 3.9.6, an empty
PadMint folder, a fresh source checkout and a fresh pip cache for each:

```sh
scripts/player-check.sh tribespad ios -
scripts/player-check.sh tribespad macos -
```

The host was Apple Silicon, macOS 26.6.2, Xcode 27.1 (27A9275), on APFS.
Xcode and the documented Homebrew prerequisites were already installed.

A recursive tree walk summed `st_blocks * 512` for allocated bytes and regular
file sizes for logical bytes, deduplicating hard links without following
symlinks. Walks were separated by two-second pauses. `TMPDIR` and
`PIP_CACHE_DIR` pointed into the measured workspace. A final `du -sk` also
counted the exported archive, which can appear after the last walk starts.

On this Mac, bare `mktemp -d` ignored `TMPDIR`: the iOS packager left its app
copy in the system temporary directory. That run's identified staging folder
was measured separately and added to the final total. The resulting combined
footprint exceeded every sampled workspace total plus the full staging copy.
PadMint's IPA validation streams the archive without extracting another app.
Temporary workspaces and the identified packaging copy were removed after
successful checks; measurement records and logs were retained privately.

These are observed footprints, not a proven minimum: brief compiler files can
fall between samples, and APFS can share extents between copies. The allowance
leaves 5.59 GiB (about 39%) above the larger observed footprint for those
differences. It excludes installation of Xcode,
SDKs and Homebrew tools, existing shared compiler caches, other applications,
and retained builds for additional targets or versions.

PadMint's catalog field `free_space_gb` uses GiB internally; its value is 20.
The recipe's `requirements.disk_gb` is also 20, but PadMint's checkout doctor
interprets that field as decimal GB. The README's 20 GiB allowance covers both
paths and is the stricter player-facing requirement.

## Build evidence, separate from gameplay

Both games were present: the iOS archive contains
`Payload/TribesPad.app/GameData/Starsiege/`, and the Mac archive contains
`Contents/MacOS/StarsiegeEngine`. Neither backend log contained
`Starsiege: Tribes could not be built`. Archive CRC checks passed.

This measurement does not add any device, controller, multiplayer or gameplay
acceptance evidence.
