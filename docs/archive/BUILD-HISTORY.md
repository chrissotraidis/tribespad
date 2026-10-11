# TribesPad

Tribes 2 build 25034 on iPad, iPhone and macOS, targeting the existing TribesNEXT community with enhanced textures and native ARM64 execution.

**This is an early port, not a playable client yet.** The current Apple app is explicitly an engine integration diagnostic. It runs two translated original game routines, exercises native controls, and can probe live community servers. Whole-game rendering, authentication and joining matches remain unfinished.

The product target is [the PRD](../Tribes2-Online-HD-Target.md). The current [execution plan](EXECUTION-PLAN.md) prioritizes integrated engine/module startup, original menus and maps, HD rendering, then authenticated multiplayer and physical Apple acceptance. [STATUS](STATUS.md) retains component evidence; instruction test counts do not establish a playable build.

## Build

Requires Xcode with iOS Simulator runtimes Python 3, and the 7-Zip CLI (`7zz`). The generated game code and downloads remain under ignored directories.

```sh
python3 -m venv .venv
.venv/bin/pip install -r scripts/requirements.txt
python3 scripts/fetch-inputs.py
# Download the encrypted HD archive using the manifest's complete Sync link;
# optionally import it with scripts/fetch-inputs.py --hd /path/yHDTextures2.0.vl2.
python3 scripts/prepare-inputs.py
.venv/bin/python scripts/build-apple.py --platform macos
.venv/bin/python scripts/build-apple.py --simulator YOUR_SIMULATOR_UUID
```

List simulator UUIDs with `xcrun simctl list devices available`. The builder refuses to launch a second simulator while another is booted. Shut down the previous test device before proceeding.

`--platform device` produces an unsigned-development iOS diagnostic bundle with an ad-hoc signature. It does not install a usable signed physical-device app; development signing and physical testing remain pending.

## Validate

```sh
.venv/bin/python scripts/lift-slices.py
clang -arch arm64 -O2 -std=c11 -I port/core -dynamiclib \
  generated/slices/tribes_slices.c -o generated/slices/libtribes_slices.dylib
.venv/bin/python scripts/test-slices.py
clang -arch arm64 -std=c11 -Wall -Wextra -Werror -I port/core \
  port/core/input.c tests/input_test.c -o generated/input-test
generated/input-test
clang -arch arm64 -std=c11 -Wall -Wextra -Werror -I port/core \
  port/core/server_query.c tests/server_query_test.c -o generated/server-query-test
generated/server-query-test
```

The Apple app's **Probe servers** button discovers endpoints from the official live list and sends native Tribes 2 UDP ping queries to two community servers. It reports the actual protocol/build and round-trip time. This does not log in or join a game.

Touch movement, independent ski/jump and jet holds, fire with drag aiming, gamepad mappings, and keyboard/mouse input are implemented in the diagnostic shell. These actions still need mapping to the translated game's own bindings and validation during gameplay. External controllers and keyboards have not been tested on a physical device.

## Inputs and reference

`inputs-manifest.json` pins downloaded installers, texture archive and extracted PE modules by SHA-256. Downloads come from the official TribesNEXT distribution and PlayT2's linked texture share. Free availability does not establish redistribution rights; no game binaries, textures or generated game code are tracked.

HaloPad is pinned in `dependencies.lock.json` and kept as an isolated reference under `ref/halopad`. Its active repository is not modified. The cached, manifest-verified HaloPad tools provide the baseline. Tribes extensions are built separately from verified cached sources and the tracked patch:

```sh
.venv/bin/python scripts/build-tribes-lifter.py
.venv/bin/python scripts/lift-image.py --tribes-tools --diagnostic
.venv/bin/python scripts/test-mmx.py
# Use the run directory printed by lift-image.py:
.venv/bin/python scripts/test-mmx-lowering.py generated/srw/YOUR_RUN
.venv/bin/python scripts/test-conditional-lowering.py generated/srw/YOUR_RUN
```

The patched instruction census has zero missing instruction sites, down from 716. Optimized mode still has 13 flag-region issues; strict conservative translation succeeds. Unsupported translation paths fail closed. Instruction-level ARM64 tests pass; a whole-game native session has not run. FTST, relocated ADD and REPNE SCASW now have actual emitted-code oracle tests. Full extended precision and x87 exception/tag integration remain pending. `--full-flags` is a conservative mode: the former 1,312 shift/rotate flag gaps now pass instruction and selected region-boundary tests. Strict conservative translation now succeeds, including FBLD, FXTRACT and FBSTP. Complete diagnostic LLVM generation and ARM64 object compilation succeed; 403 imported/helper symbols in the latest strict object still need runtime linkage. The build currently requires the pinned cached sources in `generated/tools/SRW`; clean acquisition of that baseline tool build remains a setup gate.

Additional engine validation:

```sh
.venv/bin/python scripts/test-x87.py generated/srw/YOUR_RUN
.venv/bin/python scripts/lift-image.py --tribes-tools --diagnostic --flags-hints tests/add-flags.sci
# Use the new run directory for all-flags ADD and word-scan validation:
.venv/bin/python scripts/test-runtime-lowering.py generated/srw/YOUR_FLAGS_RUN
.venv/bin/python scripts/lift-image.py --tribes-tools --diagnostic --full-flags
.venv/bin/python scripts/test-shift.py
.venv/bin/python scripts/test-shift-lowering.py generated/srw/YOUR_FULL_FLAGS_RUN
.venv/bin/python scripts/test-flag-seams.py generated/srw/YOUR_FULL_FLAGS_RUN
# Whole-image diagnostics may take substantial compiler time; observe the same worker.
.venv/bin/python scripts/compile-engine-census.py generated/srw/YOUR_FULL_FLAGS_RUN
python3 scripts/inspect-engine-object.py generated/srw/YOUR_FULL_FLAGS_RUN
# Extended80 foundation and candidate stack runtime; whole-game integration remains pending:
python3 scripts/build-softfloat.py
python3 scripts/build-bochs-math.py
.venv/bin/python scripts/test-ext80.py
.venv/bin/python scripts/test-float80-stack.py
.venv/bin/python scripts/test-float80-runtime.py
.venv/bin/python scripts/test-float80-compare.py
# Run a strict full-flags lift, then test its three formerly missing sites:
.venv/bin/python scripts/lift-image.py --tribes-tools --full-flags
.venv/bin/python scripts/test-float80-stack.py --lift generated/srw/YOUR_STRICT_RUN
python3 scripts/build-softfloat.py --platform device
python3 scripts/build-softfloat.py --platform simulator
python3 scripts/build-bochs-math.py --platform device
python3 scripts/build-bochs-math.py --platform simulator
.venv/bin/python scripts/test-bochs-math.py
.venv/bin/python scripts/check-transcendental-accuracy.py
.venv/bin/python scripts/test-math-lowering.py generated/srw/YOUR_STRICT_RUN
.venv/bin/python scripts/prepare-va-model.py generated/srw/YOUR_STRICT_RUN
.venv/bin/python scripts/test-va-model.py generated/srw/YOUR_STRICT_RUN
.venv/bin/python scripts/compile-va-model.py generated/srw/YOUR_STRICT_RUN
.venv/bin/python scripts/test-engine-bootstrap.py generated/srw/YOUR_STRICT_RUN
python3 scripts/test-native-win32.py
python3 scripts/test-startup-abi.py
python3 scripts/test-encoding-abi.py
python3 scripts/test-codepage-conversion.py
python3 scripts/build-native-win32.py --platform device
python3 scripts/build-native-win32.py --platform simulator
```

The FTST test compiles an offline x86_64 oracle and requires Rosetta on the build Mac. It also records the oracle's limitations. Device execution remains native ARM64. `scripts/update-lifter-patch.py` freezes reviewed local compiler edits into the tracked patch after checking baseline source hashes.

The pure-math x86 trace deliberately records remaining transcendental value/status differences. Its exact-match result is false; sampled Intel numeric bounds and emitted ABI checks pass separately. All required x87 symbols are implemented, but exceptional behavior and whole-game integration remain acceptance gates. The address-model test executes original SRW routines through finite native dispatch; it does not start the complete game. A separate linked whole-engine bootstrap diagnostic enters the real CRT and validates execution through exception-filter registration, native critical sections and fixed guest allocation plus descriptor-backed standard handles and duplication to the next missing service, IFC22’s `CImmDevice::CreateDevice` export, after real hidden WinSock-window creation with three original callbacks, native Winsock initialization, current-directory queries and configured C: drive enumeration/type classification, the DBCS capability query and original invalid-count semaphore paths and native asset directory enumeration and opening and reading actual archives/startup scripts. Native BSD UDP is tested separately against a local peer; async notifications, TCP/authentication and DirectInput device behavior remain unfinished. Native provider load/export routing and root/device COM reference lifetimes and capability/device-info metadata are implemented; the version 5 root creation call and original keyboard enumeration callback execute, creating its native v2 keyboard device and reading its capabilities and enumerating all 256 keyboard scan-code slots (113 specific labels). The original game builds a complete unique 256-entry keyboard format; native SetDataFormat validates and retains all mappings independently of guest buffers. The stored map matches the original request, and BYOFFSET range queries are tested. Range, buffer-size and axis-mode property queries pass separate guarded native ABI tests. Keyboard and pointer callbacks now complete; both formats and 128-slot event buffers are configured. Readonly US scan/VK mapping is implemented; 104 original MapVirtualKeyA calls and 312 ToAscii translations now complete, followed by the processed Caps Lock query. The full original text-input cache matches the service model. LoadIconA decodes the original group 107 into a shared 32x32 image and preserves its legacy AND/XOR mask; every pixel and mask byte matches an independent decode of resource 2. Native resource-cache and malformed-input tests pass, including 80,000 concurrent reads. Named/system icons, PNG and resizing remain strict gates; Apple window icon drawing is unfinished. LoadCursorA supplies an owned shared standard-arrow resource with explicit TribesPad software-theme pixels and hotspot; every native pixel/mask byte matches the profile, and 80,000 concurrent initial/cache loads pass. This does not install the pointer in Apple windows; other cursor shapes and custom resources remain strict gates. GetStockObject retains six immutable solid/null brush descriptors, and DC brush selection is independent of its selected bitmap. Stock resources survive DeleteObject; new DCs start with WHITE_BRUSH. The original black/gray requests and both graphics-class registrations complete with native descriptor agreement. Device-specific pattern realization, drawing and other stock object types remain unfinished. Apple main-thread display snapshots now seed the guest desktop from NSScreen/UIScreen backing pixels. Desktop GetDC/GetDeviceCaps/ReleaseDC own and query an opaque BGRA8 surface, enforce release ownership and reclaim contexts at thread exit. The original depth/width/height queries complete on macOS, iPad and iPhone using measured geometry. This surface has not been presented; general drawing, live rotation/mode changes and window DCs remain unfinished. Shift/Caps/control and guarded WORD translations are tested. GetKeyState and GetKeyboardState use configured thread-owned snapshots; lock toggles change only on new down transitions, modifiers aggregate left/right keys, and null-state ToAscii reads that snapshot. Tests include 640,000 concurrent transition cycles and fresh-thread isolation. Apple message delivery and physical input are still unfinished. Native mapping and event-ring storage, overflow, peek and consume tests run on macOS and both simulators; guest event delivery and acquisition remain unfinished. The original object callbacks build the control map; all live guest heap bytes now agree with the original-x86 service model. Native DX3/DX5 descriptors, type/attachment/force-feedback filters, early stop, nested callbacks and reentrant catalog replacement are tested. The native ABI tests also execute successfully in the iPad and iPhone simulators, one booted at a time. Apple event feed and device state/acquisition are still unfinished. The fixed compatibility CPU advertises tested native x87/MMX only; all four original CPU queries execute and select feature flags 7. Both legacy version-info layouts are implemented. Thirty native groups pass normal and ThreadSanitizer tests; their runtime objects compile for all three Apple targets. The full original-address engine also compiles for the iOS simulator; original CRT startup passes the same GetLogicalDriveStringsA checkpoint on iPad then iPhone, including register/flag/stack/image/heap/icon comparisons. These runs use native simulator CLI executables; UIKit interaction, visible rendering and physical-device execution remain unfinished. The x86 comparison uses an explicit single-thread Windows-service model; executing Windows DLLs remain unverified. It stops there; no menus or match have run.

To run the full-engine startup diagnostic on the sole booted iOS simulator:

```sh
.venv/bin/python scripts/compile-va-model.py generated/srw/YOUR_STRICT_RUN --platform simulator
.venv/bin/python scripts/test-engine-bootstrap.py generated/srw/YOUR_STRICT_RUN --simulator-device YOUR_BOOTED_DEVICE_UUID
```

The compiler preserves verified existing objects and refuses duplicate compilation. Unsupported APIs stop explicitly. This diagnostic does not establish a playable client.

The native default process heap owns packed x86 allocations. FLS dispatches actual ARM64 cleanup callbacks on index release and thread exit. Thirty host service groups pass normal and ThreadSanitizer tests. Sixteen service-driver executables plus separate heap, FLS, launch and encoding ABI tests pass on each simulator, tested sequentially. QoL's original-x86 diagnostic now uses native launch, heap, synchronization, encoding, classification and default case-mapping services. It completes the allocator's system-info query, kernelbase load, VirtualAlloc2FromApp lookup and provider-reference release, then loads ntdll, resolves NtAllocateVirtualMemoryEx and resolves the processor/NUMA exports, consumes the registered executable path through GetModuleFileNameW and queries the default ANSI file mode, converts the executable path and consumes and frees an owned Unicode environment snapshot and executes GetNumaHighestNodeNumber and the allocator’s actual 128 MiB reservation with 4 MiB alignment. The diagnostic now supplies a real nonzero FS-backed TEB identity, checked through native sparse FS helpers. Correcting the previous zero identity removes the allocator's repeated allocation/bad_alloc path: current startup reserves 128 MiB, commits 4 MiB and passes two native-backed GetCurrentThread calls and registers the four SDL allocator callbacks through native owned state and passes six native VirtualQuery calls and reaches a low-address executable allocation request without retry sleeps. An explicit zero-identity control reproduces the old failure with the same allocator and assets. The sampled processor/UMA topology passes actual ARM64 layout and concurrency tests. The owned virtual-memory backend and NT in/out allocation wrapper pass actual ARM64 lifecycle and fault tests on macOS, iPhone and iPad; independent 4 KiB commit/protection on 16 KiB backing pages and the original allocator's free/NT mapping bridge and memory/address-space budgeting remain unfinished. Wine-backed NLS data remains a declared compatibility profile; Windows parity, native QoL initialization, rendering, live controls and authenticated matches remain unfinished. See `docs/STATUS.md` for evidence and scope.
