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

# Minimal cfgs reflect the screens' current contracts. As of the conformance
# refactor (submodule >= 4444290: "no English content defaults / require
# localized content"), screens no longer hardcode English labels — the caller
# supplies every localized string (labels, button_list, etc.). Each entry below
# is the minimal set of required fields per that screen's inline validation.
SCREEN_MIN_CFGS = {
    "button_list_screen": {"top_nav": {"title": "T"}, "button_list": ["A"]},
    "main_menu_screen": {"top_nav": {"title": "Menu"},
                         "button_list": ["Scan", "Seeds", "Tools", "Settings"]},
    "large_icon_status_screen": {"status_type": "success", "button_list": ["OK"]},
    "keyboard_screen": {"top_nav": {"title": "K"}, "cols": 5,
                        "keys": ["1", "2", "3", "4", "5"]},
    # The C screen requires a non-null ctx (rejects NULL), so pass a dict.
    "seed_add_passphrase_screen": {"top_nav": {"title": "Enter Passphrase"}},
    "seed_mnemonic_entry_screen": {"top_nav": {"title": "Seed Word"},
                                   "wordlist": ["abandon", "ability", "able", "about", "above"],
                                   "initial_letters": "ab"},
    "seed_finalize_screen": {"top_nav": {"title": "Finalize Seed"},
                             "fingerprint": "0abc1234", "fingerprint_label": "Fingerprint",
                             "button_list": ["Done"]},
    "seed_export_xpub_details_screen": {"top_nav": {"title": "Xpub Details"},
                                        "fingerprint": "0abc1234",
                                        "derivation_path": "m/84'/0'/0'",
                                        "xpub": "zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG",
                                        "fingerprint_label": "Fingerprint",
                                        "derivation_label": "Derivation",
                                        "xpub_label": "Xpub", "button_list": ["Export"]},
    "seed_review_passphrase_screen": {"top_nav": {"title": "Review Passphrase"},
                                      "passphrase": "hunter2",
                                      "fingerprint_without": "0abc1234",
                                      "fingerprint_with": "5def6789",
                                      "changes_fingerprint_label": "Changes fingerprint to",
                                      "button_list": ["Done"]},
    "seed_words_screen": {"top_nav": {"title": "Seed Words"},
                          "words": ["abandon", "ability", "able", "about"],
                          "button_list": ["Done"]},
    "seed_transcribe_whole_qr_screen": {"top_nav": {"title": "Transcribe"},
                                        "qr_data": _SEEDQR_DIGITS, "qr_mode": "numeric",
                                        "button_list": ["Done"]},
    "seed_transcribe_zoomed_qr_screen": {"qr_data": _SEEDQR_DIGITS, "qr_mode": "numeric",
                                         "exit_text": "Done"},
    # show_brightness_tips defaults true, which then requires the tip texts;
    # disable it for the minimal build.
    "qr_display_screen": {"qr_data": _SEEDQR_DIGITS, "qr_mode": "numeric",
                          "show_brightness_tips": False},
    "opening_splash_screen": None,
    "loading_spinner_screen": None,
    "psbt_overview_screen": {"top_nav": {"title": "Review PSBT"}, "num_inputs": 1,
                             "destination_addresses": [_ADDR], "button_list": ["Details"]},
    "psbt_address_details_screen": {"top_nav": {"title": "Send"}, "address": _ADDR,
                                    "button_list": ["Next"]},
    "psbt_change_details_screen": {"top_nav": {"title": "Change"}, "address": _ADDR,
                                   "address_type_label": "Address type",
                                   "btc_amount": {"network": "M"},
                                   "button_list": ["Next"]},
    "psbt_math_screen": {"top_nav": {"title": "Math"},
                         "amounts": {"input": "1.0", "spend": "0.9",
                                     "fee": "0.01", "change": "0.09"},
                         "labels": {"inputs": "Inputs", "recipients": "Recipients",
                                    "fee": "Fee", "change": "Change"},
                         "button_list": ["Next"]},
    "psbt_op_return_screen": {"top_nav": {"title": "OP_RETURN"}, "text": "hello",
                              "button_list": ["Next"]},
    "multisig_wallet_descriptor_screen": {"top_nav": {"title": "Multisig"},
                                          "policy": "2 of 3", "policy_label": "Policy",
                                          "signing_keys_label": "Signing keys",
                                          "fingerprints": ["0abc1234", "5def6789"],
                                          "button_list": ["OK"]},
    "seed_address_verification_screen": {"top_nav": {"title": "Verify Address"},
                                         "address": _ADDR, "type_network": "P2WPKH mainnet",
                                         "button_list": ["Next"]},
    "seed_sign_message_confirm_address_screen": {"top_nav": {"title": "Confirm Address"},
                                                 "derivation_path": "m/84'/0'/0'/0/0",
                                                 "derivation_path_label": "Derivation",
                                                 "address": _ADDR, "button_list": ["Next"]},
    "seed_sign_message_confirm_message_screen": {"top_nav": {"title": "Confirm Message"},
                                                 "message": "hello world",
                                                 "button_list": ["Next"]},
    "settings_qr_confirmation_screen": {"top_nav": {"title": "Settings QR", "show_back_button": False},
                                        "status_message": "Setting updated",
                                        "button_list": ["OK"]},
    "settings_locale_picker_screen": {"top_nav": {"title": "Language"},
                                      "active_locale": "en",
                                      "rows": [{"locale": "en", "english": "English", "native": "English"}]},
    "tools_address_explorer_address_list_screen": {"top_nav": {"title": "Addresses"},
                                                   "addresses": [_ADDR], "start_index": 0,
                                                   "next_label": "Next"},
    "tools_calc_final_word_screen": {"top_nav": {"title": "Final Word Calc"},
                                     "your_input_text": "abandon ability able",
                                     "checksum_label": "Checksum",
                                     "final_word_text": "Final word is",
                                     "selected_final_bits": "10111111011",
                                     "checksum_bits": "1111", "button_list": ["Next"]},
    "tools_calc_final_word_done_screen": {"top_nav": {"title": "Final Word"},
                                          "final_word": "zoo", "fingerprint": "0abc1234",
                                          "fingerprint_label": "Fingerprint",
                                          "button_list": ["Done"]},
    "reset_screen": {"top_nav": {"title": "Reset"}, "text": "Restarting..."},
    "power_off_not_required_screen": {"top_nav": {"title": "Power Off"},
                                      "text": "Power off not required"},
    "donate_screen": {"top_nav": {"title": "Donate"}, "text": "Support SeedSigner",
                      "url": "https://seedsigner.com"},
    "io_test_screen": {"top_nav": {"title": "I/O Test"}, "capturing_text": "Testing...",
                       "clear_label": "Clear", "exit_label": "Exit"},
    "camera_preview_screen": {"instructions_text": "< back  |  Scan a QR code"},
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
