"""Smoke tests for the native cUR `uUR` extension.

Built as a SEPARATE extension from seedsigner_lvgl_screens (see setup.py) so the
app's helpers/ur2/decoder.py `__import__("uUR")` gets native BC-UR encode/decode
on the Pi, mirroring the ESP32 firmware's native uUR module. Skips when uUR isn't
built into the test environment (e.g. host pytest without the cross-build).
"""
import importlib

import pytest


def _uur_or_skip():
    try:
        return importlib.import_module("uUR")
    except ModuleNotFoundError:
        pytest.skip("uUR (native cUR) not built/installed in this test environment")


def test_uur_multipart_fountain_roundtrip():
    uUR = _uur_or_skip()

    payload = bytes((i * 7 + 3) & 0xFF for i in range(120))
    cbor = uUR.Types.bytes_to_cbor(payload)
    assert uUR.Types.bytes_from_cbor(cbor) == payload

    ur = uUR.UR("bytes", cbor)
    assert ur.type == "bytes"
    assert bytes(ur.cbor) == cbor

    enc = uUR.UREncoder(ur, max_fragment_len=30, first_seq_num=0, min_fragment_len=10)
    assert not enc.is_single_part()
    seq_len = enc.fountain_encoder.seq_len()
    assert seq_len > 1

    dec = uUR.URDecoder()
    state = uUR.DECODER_PROCESSING
    for _ in range(seq_len * 4 + 10):
        state = dec.receive_part(enc.next_part())
        if state == uUR.DECODER_OK:
            break

    assert state == uUR.DECODER_OK
    assert dec.state == uUR.DECODER_OK
    assert dec.result is not None
    assert dec.result.type == "bytes"
    assert bytes(dec.result.cbor) == cbor
    assert uUR.Types.bytes_from_cbor(dec.result.cbor) == payload
    assert dec.expected_part_count == seq_len
    assert 0.0 <= dec.estimated_percent_complete() <= 1.0


def test_uur_single_part_roundtrip():
    uUR = _uur_or_skip()

    # Payload must be >= min_fragment_len (default 10); cUR rejects smaller.
    payload = b"single-part payload of forty-ish bytes!!"
    ur = uUR.UR("bytes", uUR.Types.bytes_to_cbor(payload))
    enc = uUR.UREncoder(ur, max_fragment_len=100)
    assert enc.is_single_part()

    dec = uUR.URDecoder()
    assert dec.receive_part(enc.next_part()) == uUR.DECODER_OK
    assert uUR.Types.bytes_from_cbor(dec.result.cbor) == payload


def test_uur_junk_frame_returns_error_state():
    # A malformed frame yields an error STATE, not an exception — junk/misread
    # frames are expected during a QR scan loop. (DECODER_OK == 0 is falsy;
    # callers must compare against the DECODER_* constants.)
    uUR = _uur_or_skip()
    dec = uUR.URDecoder()
    st = dec.receive_part("ur:bytes/not-a-real-part")
    assert st != uUR.DECODER_OK
    assert dec.result is None


def test_uur_bad_ur_arg_type_raises():
    uUR = _uur_or_skip()
    with pytest.raises(TypeError):
        uUR.UREncoder("not a UR object", max_fragment_len=100)
