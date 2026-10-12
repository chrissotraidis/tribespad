# Public launch checklist

TribesPad stays private for now. Everything that can be checked without a public
repository is done or listed under "Before". The steps under "After" need the
repository to be public, in this order.

## Before making the repository public

- [ ] **Choose a license.** The README says all rights are reserved until one is added.
      Third-party notices for bundled code are in [THIRD-PARTY.md](THIRD-PARTY.md).
- [ ] **Decide on the translation patches.** `patches/translation` (3.5 MB) holds
      binary differences against the player's own lift of the official files, like a ROM
      patch. They are useless without the downloads, but they are derived from the game
      and from TribesNEXT; publishing them is the owner's call.
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
5. **Device acceptance.** Sign the IPA with a profile that allows Extended Virtual
   Addressing, install it on an iPad and an iPhone, and play an offline match. Mark the
   `macos-arm64` host `verified` in the recipe only after a device accepts it.
6. **Update the README status** (runner tested, PadMint available, device results) and
   [HANDOFF](HANDOFF.md).
