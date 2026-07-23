# Native zbar needs `ZBAR_CFG_BINARY` for byte-mode QR (CompactSeedQR)

## Symptom
On the Pi, **CompactSeedQR** codes fail to scan (never decode into a seed), while
**numeric SeedQR** codes scan fine. Every other QR type that carries text (PSBT
base64/UR, addresses, settings) also works. Only raw-binary payloads are affected.

## Root cause
The Pi's QR scanning is fully native: `run_camera_scan` (app) →
`camera_scanner` C module → `native/camera/camera_engine.cpp`, which decodes camera
frames with **libzbar** and hands Python the decoded payload bytes. (pyzbar is NOT in
this path — it belonged to the older PIL/host-decode pipeline.)

libzbar, by default, **converts byte-mode QR data to text** (an ECI/charset
conversion on the decoded bytes). CompactSeedQR encodes 16 or 32 bytes of raw entropy
in QR **byte mode**; that entropy routinely contains non-ASCII / NUL bytes, which the
text conversion mangles. The corrupted bytes no longer match a valid CompactSeedQR, so
`DecodeQR` never recognizes it. Numeric SeedQR is all ASCII digits, so the same
conversion is a no-op — which is exactly why it kept working and masked the bug.

zbar exposes an opt-out: `ZBAR_CFG_BINARY`, documented in `zbar.h` as
*"don't convert binary data to text."* The scanner setup in `camera_engine.cpp`
enabled QRCODE but never set this config, so byte-mode payloads were silently
corrupted.

## The fix
One line, on the QRCODE symbol, at scanner setup (alongside `ZBAR_CFG_ENABLE`):

```c
zbar_image_scanner_set_config(g->zbar, ZBAR_QRCODE, ZBAR_CFG_BINARY, 1);
```

## Why only that one line
The rest of the native byte path was already correct — this was a single-point gap:
- `zbar_symbol_get_data` + `zbar_symbol_get_data_length` (length-delimited, **not**
  `strlen`) → NUL bytes survive.
- `scan_coord_on_frame(payload, len)` stores a `std::vector<uint8_t>` and dedups with
  `memcmp` over the length → binary-safe.
- `camera_scanner.poll_new()` returns real Python `bytes` via
  `PyBytes_FromStringAndSize` → not a decoded `str`.
- Python `DecodeQR.add_data(bytes)` handles the CompactSeedQR byte path directly.

So once zbar stops text-converting the payload, raw entropy flows through unmodified.

## Parity note (why upstream didn't hit this)
Upstream SeedSigner decodes with the **SeedSigner pyzbar fork**
(`git+https://github.com/seedsigner/pyzbar`), whose `binary=True` sets this *same*
zbar config. `decode_qr.py` calls `pyzbar.decode(image, ..., binary=is_binary)`. The
native engine has to set `ZBAR_CFG_BINARY` itself to match that behavior — this fix is
the native equivalent of `binary=True`.

## Cross-platform note
This applies to the **Pi (libzbar)** decoder only. The ESP32 build decodes with a
**different library (k_quirc)**, not zbar, and is already confirmed to read
CompactSeedQR correctly — so no equivalent change is needed there.
