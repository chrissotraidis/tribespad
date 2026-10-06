The classification and default case data in ../../nls_classification.inc and
../../nls_casing.inc are extracted from the
published Wine 10.0 nls/sortdefault.nls file. The generator retains its compact
character-type records and index; no character classes are inferred from the
host's locale or Unicode library.

Source: https://github.com/wine-mirror/wine/blob/wine-10.0/nls/sortdefault.nls
Source SHA-256: 8f92004f6d245648e98a5b6b206e8fdb3dfe6694251692590f5f9aae1552a943
Format reference: Wine 10.0 dlls/kernelbase/locale.c
Reproduction: scripts/gen-nls-classification.py and scripts/gen-nls-casing.py
Provenance and reference hashes: config/encoding-sources.json

LICENSE preserves the Wine repository's COPYING.LIB notice. The original
reader source and NLS file are pinned under ref/downloads for reproduction.
This is a Wine-compatible data profile, not verification against Windows DLLs
or proof that the table matches an XP-era NLS version. Supplementary Unicode
scalars are not combined from surrogate pairs by the current helper.

Default collation data and comparison source are adapted into
../../nls_collation.inc by scripts/gen-nls-collation.py. The source algorithm
comes from the pinned Wine 10.0 dlls/kernelbase/locale.c, with original copyright
and LGPL notice retained. Native heap allocation replaces RtlAllocateHeap;
allocation failure stops explicitly. The generator validates the default GUID
has no compression or exception tables before specializing those branches.
Default UTF-16 weights, 730 expansions and 357 Jamo records are retained.
English/invariant locale selection and bounded guest input validation live in
../../guest_collation.c; other locales and optional NLS contexts are explicit
unsupported boundaries. No host locale sorting library is used.

The same generator adapts 50 ASCII regression expectations from the pinned
Wine kernel32 locale tests into tests/fixtures/collation_cases.inc for UTF-16
input. The legacy CompareStringA-specific numeric-flag rejection is excluded;
CompareStringEx numeric sorting is tested separately. These are reference
expectations, not evidence of executing Windows DLLs. All raw sources, hashes
and URLs are in config/encoding-sources.json.

Locale enumeration uses Wine 10.0 nls/locale.nls and include/winternl.h layout
metadata, pinned in config/encoding-sources.json. scripts/gen-nls-locales.py
extracts ordered names, alternate-sort markers and inverted-neutral flags,
excluding alias records as the original reader does. The complete extracted
profile and skipped aliases are exposed in config/locale-enumeration-reference.json.
This is a declared Wine locale population, not an inventory of Windows or Apple
installed locales. The generator emits ../../nls_locales.inc; native iteration
and guest callback dispatch are in ../../guest_locales.c and
../../guest_locale_callback.c. Original reader license/copyright provenance is
retained in the pinned source and this LICENSE file.

Date/time presentation data in ../../nls_datetime.inc is extracted from the same
pinned locale.nls by scripts/gen-nls-datetime.py: 944 profiles and 1087 names
including aliases. Date/time pattern handling in ../../guest_datetime.c adapts
the pinned locale reader; DATETIME-NOTICE retains its original copyright and
license notice. Absent genitive arrays fall back to ordinary month arrays.
The native profile accepts Gregorian dates only, with explicit unsupported
boundaries for alternate calendars and reading-layout flags. Single-year and
era token grouping follow the Microsoft format-picture documentation. No
registry customizations or actual Windows DLL execution parity are claimed.

Locale information in ../../nls_info.inc is extracted by scripts/gen-nls-info.py
from the same pinned locale.nls and layout header. Numeric and derived-field
rules adapt the pinned locale reader, with original notices retained in
LOCALE-INFO-NOTICE. The immutable profile has 144 base information fields and
26 genitive month variants. English localized display aliases follow the source
reader's declared localization limitation. Registry user overrides and localized
sort descriptions are not implemented. No Apple locale substitution is used.
The locale information wrapper's invariant golden tests come independently
from the pinned Wine invariant_list; that list explicitly permits an empty
invariant country abbreviation. Binary font signature expectations are separately
sliced from the pinned source record, not generated from native service output.

Locale identifier indexes in ../../nls_identifiers.inc come from the same pinned
locale.nls via scripts/gen-locale-identifiers.py. The full name/ID indexes and
neutral-resolution records are exposed in config/locale-identifiers-reference.json.
The four services in ../../guest_locale_identifiers.c adapt the pinned reader's
case and underscore/hyphen equivalence, neutral-name resolution and default-ID
handling. Original reader copyright/license notices are retained in
LOCALE-INFO-NOTICE. User/system defaults are the declared en-US runtime profile.
Reserved flag bits are rejected by the declared contract; source Wine ignores
some such bits. Shared/custom IDs are not a bijective locale-name mapping, and
these data/profile rules are not actual Windows or Apple locale parity.
