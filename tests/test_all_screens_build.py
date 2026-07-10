"""Every exported *_screen must build headlessly with its minimal cfg.

This is the repo's dlopen/link/regression tripwire: a screen source dropped
from the setup.py glob, an entry point renamed in the submodule, or a binding
whose minimal cfg no longer builds all fail here — inside the ARMv6 build's
pytest gate — instead of on the device.

SCREEN_MIN_CFGS is intentionally exhaustive: adding a screen binding without
adding its row fails test_screen_table_is_exhaustive. A value of None means
"call with no cfg argument" (optional-cfg and no-arg screens).

The LVGL runtime is initialized ONCE for the whole module (the `native` fixture)
and every screen is built into that single runtime, sequentially — exactly how
the app uses the extension (init once, load many screens). Per-test
init/shutdown cycles would exercise repeated lv_init/lv_deinit, a path the app
never takes and a known source of instability.
"""
import importlib

import pytest


@pytest.fixture(scope="module")
def native():
    try:
        mod = importlib.import_module("seedsigner_lvgl_screens")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_screens not built/installed in this test environment")
    mod.lvgl_init(hor_res=240, ver_res=240)
    yield mod
    mod.lvgl_shutdown()


_ADDR = "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"
_SEEDQR_DIGITS = "0015108016251442000617751481122306221491"

SCREEN_MIN_CFGS = {
    "button_list_screen": {"top_nav": {"title": "T"}, "button_list": ["A"]},
    "main_menu_screen": None,
    "large_icon_status_screen": {"status_type": "success", "button_list": ["OK"]},
    "keyboard_screen": {"top_nav": {"title": "K"}, "cols": 5,
                        "keys": ["1", "2", "3", "4", "5"]},
    # The C screen requires a non-null ctx (rejects NULL), so pass a dict.
    "seed_add_passphrase_screen": {"top_nav": {"title": "Enter Passphrase"}},
    "seed_mnemonic_entry_screen": {"top_nav": {"title": "Seed Word"},
                                   "wordlist": ["abandon", "ability", "able", "about", "above"],
                                   "initial_letters": "ab"},
    "seed_finalize_screen": {"fingerprint": "0abc1234"},
    "seed_export_xpub_details_screen": {"fingerprint": "0abc1234", "xpub": "zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG"},
    "seed_review_passphrase_screen": {"passphrase": "hunter2"},
    "seed_words_screen": {"words": ["abandon", "ability", "able", "about"]},
    "seed_transcribe_whole_qr_screen": {"qr_data": _SEEDQR_DIGITS, "qr_mode": "numeric"},
    "seed_transcribe_zoomed_qr_screen": {"qr_data": _SEEDQR_DIGITS, "qr_mode": "numeric"},
    # show_brightness_tips defaults true, which then requires the tip texts;
    # disable it for the minimal build.
    "qr_display_screen": {"qr_data": _SEEDQR_DIGITS, "qr_mode": "numeric",
                          "show_brightness_tips": False},
    "opening_splash_screen": None,
    "loading_spinner_screen": None,
    "psbt_overview_screen": {"num_inputs": 1, "destination_addresses": [_ADDR]},
    "psbt_address_details_screen": {"address": _ADDR},
    "psbt_change_details_screen": {"address": _ADDR},
    "psbt_math_screen": {},
    "psbt_op_return_screen": {},
    "multisig_wallet_descriptor_screen": {},
    "seed_address_verification_screen": {"address": _ADDR, "type_network": "P2WPKH mainnet"},
    "seed_sign_message_confirm_address_screen": {"derivation_path": "m/84'/0'/0'/0/0", "address": _ADDR},
    "seed_sign_message_confirm_message_screen": {"message": "hello world"},
    "settings_qr_confirmation_screen": {"top_nav": {"title": "Settings QR", "show_back_button": False}},
    "settings_locale_picker_screen": {"top_nav": {"title": "Language"},
                                      "active_locale": "en",
                                      "rows": [{"locale": "en", "english": "English", "native": "English"}]},
    "tools_address_explorer_address_list_screen": {"addresses": [_ADDR], "start_index": 0},
    "tools_calc_final_word_screen": {"top_nav": {"title": "Final Word Calc"},
                                     "selected_final_bits": "10111111011",
                                     "checksum_bits": "1111"},
    "tools_calc_final_word_done_screen": {"final_word": "zoo", "fingerprint": "0abc1234"},
    "reset_screen": {},
    "power_off_not_required_screen": {},
    "donate_screen": {},
    "io_test_screen": {},
    "screensaver_screen": None,
}

# Runtime helpers that share the _screen suffix but are not screen builders.
_NON_BUILDER_EXPORTS = {"save_screen", "restore_screen", "clear_screen"}


def test_screen_table_is_exhaustive(native):
    exported = {
        name for name in dir(native)
        if name.endswith("_screen") and name not in _NON_BUILDER_EXPORTS
    }
    assert exported == set(SCREEN_MIN_CFGS), (
        "screen exports and SCREEN_MIN_CFGS disagree; update the table "
        "(and docs/interface-contract.md) for any added/removed screen"
    )


@pytest.mark.parametrize("name", sorted(SCREEN_MIN_CFGS))
def test_screen_builds_with_minimal_cfg(native, name):
    native.clear_result_queue()
    cfg = SCREEN_MIN_CFGS[name]
    fn = getattr(native, name)
    if cfg is None:
        fn()
    else:
        fn(cfg)
    assert native._debug_last_path() == "compiled"
    # Advance timers briefly so builder-registered animations/timers run once.
    native.lvgl_pump(duration_ms=20)
