# Public launch checklist

Decisions (21 October 2026): MIT license for TribesPad's code, keep the translation
patches, no prebuilt IPA. Everything that can be checked without a public repository
is done or listed under "Before". The steps under "After" need the
repository to be public, in this order.

## Before making the repository public

- [x] **License:** MIT ([LICENSE](../LICENSE)), covering TribesPad's code only; the README
      says the game and TribesNEXT files are not covered. Third-party notices are in
      [THIRD-PARTY.md](THIRD-PARTY.md).
- [x] **Translation patches:** kept. They apply only to the player's own lift of the
      official downloads.
- [x] **No prebuilt IPA:** players build their own (PadMint or the script).
- [x] Physical iPad Pro 12.9 (6th gen) and iPhone 14: install, TribesNEXT sign-in,
      online Classic CTF with touch and an Xbox controller.
- [ ] Merge `codex/menu-icon-ios27` into `main`.
- [x] Repository scan for personal paths, secrets and game files (see "Repository scan").
- [x] One-command build from a clean clone (iPhone/iPad IPA and Mac zip).
- [x] PadMint player check from a local checkout (`scripts/player-check.sh tribespad ios - CHECKOUT`):
      Apple's Python 3.9, an empty PadMint folder and a fresh copy of the source built the
      IPA in 32 minutes; the checkout stayed clean and PadMint's publication gate passed.
- [x] iPhone 17 Simulator: offline match from a fresh install.

## After it is public

1. **Tag a release.** PadMint reads the recipe from the latest release:
   `git tag v0.1.0 && git push origin v0.1.0`, then create the GitHub release
   without binaries (`padmint.json` says `public_binaries: false`).
2. **Run the GitHub workflow once.** Actions → *Build TribesPad for iPhone and iPad* →
   Run workflow. Record the run time, peak memory and whether the IPA artifact appears.
   The runner has 7 GB of memory and one build process peaked at 6.3 GB here; if it
   runs out, lower parallelism in the engine compile before anything else.
3. **PadMint player check from the release:**
   `scripts/player-check.sh tribespad ios -` and `... macos -` in the PadMint
   repository (the `-` means no game file).
4. **Catalog.** Run `PADMINT_HOME=$(mktemp -d) python3 scripts/audit-catalog.py`, then
   take [PadMint PR #149](https://github.com/chrissotraidis/padmint/pull/149) out of
   draft and merge it. Set `padmint.json`'s `status` to `experimental`
   in the next TribesPad release.
5. **Device acceptance from the release build.** Install the IPA that the release
   produces on an iPad and an iPhone and play online. Mark the `macos-arm64` host
   `verified` in the recipe only after that.
6. **Update the README status** (runner tested, PadMint available, device results) and
   [HANDOFF](HANDOFF.md).
