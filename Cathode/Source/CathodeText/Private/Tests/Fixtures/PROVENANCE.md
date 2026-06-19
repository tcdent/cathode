# Test fixture provenance — LiberationSans-Regular.ttf

A real TrueType font, committed **unmodified** as a **test-only fixture** for sprint 021 (the first real
HarfBuzz `hb_shape` execution + the kerning-relation proof). NOT shipped in the product — read by
`FFontRegistry::RegisterFontFile` → `FFileHelper::LoadFileToArray` → `hb_blob_create`.

- **File:** `LiberationSans-Regular.ttf` — 410712 bytes
- **sha256:** `76d04c18ea243f426b7de1f3ad208e927008f961dc5945e5aad352d0dfde8ee8`
- **Family / version:** Liberation Sans (Regular), **version 2.1.5** — the OFL v2.x series (the
  croscore-relicensed Liberation). Metric-compatible with Arial; has GPOS/kern data (verified empirically:
  `hb_shape` of `"AV"` kerns below `"A"`+`"V"`).
- **Source:** the official **liberation-fonts v2.1.5** release —
  <https://github.com/liberationfonts/liberation-fonts> (`liberation-fonts-ttf-2.1.5.tar.gz`).
- **Upstream:** the Liberation fonts project (Red Hat, Inc.).
- **License:** **SIL Open Font License, Version 1.1** (the v2.x series license). Full text:
  [`LICENSE-LiberationSans.txt`](LICENSE-LiberationSans.txt). The OFL permits bundling this test fixture.
  (browse-design ruled OFL 2.x — this matches the ruling.)
- **Binary-verbatim:** committed via the scoped `.gitattributes` (`*.ttf binary`) so the bytes survive the
  Windows-box checkout (a CRLF/eol-mangled `.ttf` → corrupt font → `hb_shape` garbage).
