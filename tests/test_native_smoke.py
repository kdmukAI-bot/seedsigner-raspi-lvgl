import importlib
import pytest


def _native_or_skip():
    try:
        return importlib.import_module("seedsigner_lvgl_screens")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_screens not built/installed in this test environment")


def test_native_module_import_and_queue_shape():
    mod = _native_or_skip()

    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    cfg = {
        "top_nav": {"title": "Menu"},
        "button_list": ["Compiled Path"],
    }
    mod.button_list_screen(cfg)

    # Pure builder: the screen is built on the compiled path and returns
    # immediately. With no input (and no timeout-fallback synthesis), the result
    # queue stays empty until the unified Python loop pumps LVGL and real input
    # arrives.
    assert mod._debug_last_path() == "compiled"
    assert mod.poll_for_result() is None

    # Queue shape is still exercised via an injected result — the same plumbing the
    # async callback path uses when real input lands.
    mod._debug_emit_result("First", 0)
    ev = mod.poll_for_result()
    assert ev is not None
    assert isinstance(ev, tuple) and len(ev) == 3
    assert ev[0] in ("button_selected", "topnav_back", "topnav_power")
    mod.lvgl_shutdown()


def test_passphrase_screen_renders():
    mod = _native_or_skip()

    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Pure builder: builds the passphrase screen and returns immediately (the
    # unified Python loop pumps LVGL and polls for the entered text).
    mod.seed_add_passphrase_screen({
        "top_nav": {"title": "Enter Passphrase"},
        "initial_text": "satoshi",
    })
    assert mod._debug_last_path() == "compiled"
    mod.lvgl_shutdown()


def test_keyboard_screen_variants_render():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # BIP-85 child index: plain digit grid with an in-grid save button.
    mod.keyboard_screen({
        "top_nav": {"title": "BIP-85 Index"},
        "cols": 5,
        "keys": ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
        "show_save_button": True,
    })
    assert mod._debug_last_path() == "compiled"

    # Dice roll: icon-font glyphs (PUA codepoints) mapped to values, auto-return
    # after N chars, live per-keystroke title.
    dice = {
        "\uf525": "1", "\uf528": "2", "\uf527": "3",
        "\uf524": "4", "\uf523": "5", "\uf526": "6",
    }
    mod.keyboard_screen({
        "top_nav": {"title": "Dice Roll 1/50"},
        "cols": 3,
        "keys": list(dice.keys()),
        "keys_to_values": dice,
        "keyboard_font": "icon",
        "return_after_n_chars": 50,
        "title_keystroke_template": "Dice Roll {n}/{total}",
    })
    assert mod._debug_last_path() == "compiled"

    # cfg must be a dict.
    with pytest.raises(RuntimeError):
        mod.keyboard_screen("not a dict")

    mod.lvgl_shutdown()


def test_seed_mnemonic_entry_screen_renders():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    mod.seed_mnemonic_entry_screen({
        "top_nav": {"title": "Seed Word #3"},
        "wordlist": ["abandon", "ability", "able", "muffin", "mule", "muscle",
                     "museum", "mushroom", "music"],
        "initial_letters": "mus",
    })
    assert mod._debug_last_path() == "compiled"

    # wordlist is required — the native side raises without it.
    with pytest.raises(RuntimeError):
        mod.seed_mnemonic_entry_screen({"top_nav": {"title": "Seed Word #1"}})

    mod.lvgl_shutdown()


def test_qr_display_screen_static_and_animated():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Static SeedQR (numeric mode).
    mod.qr_display_screen({
        "qr_data": "0015108016251442000617751481122306221491",
        "qr_mode": "numeric",
        "data_encoding": "utf8",
        "border": 2,
        "initial_brightness": 62,
        "show_brightness_tips": True,
        "brighter_text": "Brighter",
        "darker_text": "Darker",
    })
    assert mod._debug_last_path() == "compiled"

    # No QR screen tip is active in this headless build path.
    assert mod.qr_display_is_tip_active() in (True, False)

    # Animated: push frames as str (UTF-8) and bytes; both are safe no-ops if the
    # native ctx isn't live, and must not raise.
    mod.qr_display_set_frame("B$2P0300OBZWE5H7V3EPWSNR2VJKQMFHZA55FP3WU3KRM6XXAQD5")
    mod.qr_display_set_frame(b"\x01\xf0\xe3\x2c\xda\x20\x0d\xbb")

    # Wrong frame type is a TypeError.
    with pytest.raises(TypeError):
        mod.qr_display_set_frame(123)

    # qr_data is required.
    with pytest.raises(RuntimeError):
        mod.qr_display_screen({"qr_mode": "byte"})

    mod.lvgl_shutdown()


def test_splash_screen_renders():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Defaults (no cfg) reproduce the English opening splash.
    mod.splash_screen()
    assert mod._debug_last_path() == "compiled"

    # Optional cfg merge-patches version + partner band + boot-logo handoff.
    mod.splash_screen({
        "version": "0.9.0",
        "show_partner_logos": True,
        "sponsor_text": "With support from:",
        "logo_already_shown": True,
    })
    assert mod._debug_last_path() == "compiled"

    # A non-dict cfg is rejected.
    with pytest.raises(RuntimeError):
        mod.splash_screen("not a dict")

    mod.lvgl_shutdown()


def test_splash_complete_routes_as_button_selected():
    mod = _native_or_skip()
    mod.clear_result_queue()

    # SEEDSIGNER_RET_SPLASH_COMPLETE (1101) is a host-handled lifecycle event, not a
    # Python-routed body button. It surfaces like screensaver dismiss: a
    # button_selected with index -1, the label identifying it — NOT ("button_selected",
    # 1101, ...) as a stray default would produce.
    mod._debug_emit_result("splash_complete", 1101)
    assert mod.poll_for_result() == ("button_selected", -1, "splash_complete")
    assert mod.poll_for_result() is None


def test_seed_finalize_screen_renders():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Fingerprint readout + bottom button list (Done / BIP-39 Passphrase). Buttons
    # emit button_selected; cfg requires a "fingerprint" string.
    mod.seed_finalize_screen({
        "top_nav": {"title": "Finalize Seed"},
        "fingerprint": "6a4f2e1b",
        "fingerprint_label": "fingerprint",
        "button_list": ["Done", "BIP-39 Passphrase"],
    })
    assert mod._debug_last_path() == "compiled"

    # Bare cfg still renders (title/button_list default in the native screen).
    mod.seed_finalize_screen({"fingerprint": "0abc1234"})
    assert mod._debug_last_path() == "compiled"

    # fingerprint is required — the native side raises without it.
    with pytest.raises(RuntimeError):
        mod.seed_finalize_screen({"top_nav": {"title": "Finalize Seed"}})
    # cfg must be a dict.
    with pytest.raises(RuntimeError):
        mod.seed_finalize_screen("not a dict")

    mod.lvgl_shutdown()


def test_large_icon_custom_status_renders():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Preset status_type (baked icon + color).
    mod.large_icon_status_screen({
        "top_nav": {"title": "Backup Verified", "show_back_button": False},
        "status_type": "success",
        "status_headline": "Success!",
        "text": "All mnemonic backup words were verified.",
        "button_list": ["OK"],
    })
    assert mod._debug_last_path() == "compiled"

    # "custom" status_type: the caller supplies the hero glyph + color. SIGN (U+E921,
    # PSBTFinalize) and MICROSD (U+E91F) both live in the baked 48px seedsigner icon
    # font (PUA 0xE900-0xE923), so they render on the Pi hero-icon font.
    mod.large_icon_status_screen({
        "top_nav": {"title": "Sign Transaction", "show_back_button": True},
        "status_type": "custom",
        "icon": "",            # SeedSignerIconConstants.SIGN
        "icon_color": "#ff9416",
        "status_headline": "Click to sign",
        "button_list": ["Sign"],
    })
    assert mod._debug_last_path() == "compiled"

    # The microSD-notification custom icon renders the same way.
    mod.large_icon_status_screen({
        "top_nav": {"title": "microSD", "show_back_button": False},
        "status_type": "custom",
        "icon": "",            # SeedSignerIconConstants.MICROSD
        "status_headline": "Insert microSD",
        "button_list": ["OK"],
    })
    assert mod._debug_last_path() == "compiled"

    # cfg must be a dict.
    with pytest.raises(RuntimeError):
        mod.large_icon_status_screen("not a dict")

    mod.lvgl_shutdown()


def test_loading_screen_renders():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Fire-and-forget pure builder: no result, no poll loop. Defaults (no cfg) and the
    # optional status text both build; a non-dict cfg is rejected.
    mod.loading_screen()
    assert mod._debug_last_path() == "compiled"
    mod.loading_screen({"text": "Parsing PSBT..."})
    assert mod._debug_last_path() == "compiled"
    # It produces no terminal event (would block run_lvgl_screen's poll loop).
    assert mod.poll_for_result() is None
    with pytest.raises(RuntimeError):
        mod.loading_screen("not a dict")

    mod.lvgl_shutdown()


def test_psbt_screens_render():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # Overview: animated inputs->center bar->destinations pictogram + BtcAmount
    # headline. Host describes the transaction STRUCTURE + already-formatted amount.
    mod.psbt_overview_screen({
        "top_nav": {"title": "Review Transaction"},
        "btc_amount": {"primary": "841,234", "unit": "sats", "network": "M"},
        "num_inputs": 3,
        "destination_addresses": [
            "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
            "3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy",
        ],
        "num_change_outputs": 1,
        "button": "Review details",
    })
    assert mod._debug_last_path() == "compiled"

    # Address details: amount over the full wrapped recipient address.
    mod.psbt_address_details_screen({
        "top_nav": {"title": "Verify Send Address"},
        "btc_amount": {"primary": "0.00841234", "unit": "btc", "network": "M"},
        "address": "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
        "button_list": ["Next"],
    })
    assert mod._debug_last_path() == "compiled"

    # Change details: amount + address-type label + single-line address + verified line.
    mod.psbt_change_details_screen({
        "top_nav": {"title": "Your Change"},
        "btc_amount": {"primary": "0.00123456", "unit": "btc", "network": "M"},
        "address": "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
        "address_type_label": "change address #0",
        "is_verified": True,
        "verified_text": "Address verified!",
        "button_list": ["Done"],
    })
    assert mod._debug_last_path() == "compiled"

    # Math: input - recipients - fee = change; host passes formatted number strings.
    mod.psbt_math_screen({
        "top_nav": {"title": "Transaction Math"},
        "denomination": "sats",
        "num_recipients": 1,
        "amounts": {"input": "1,000,000", "spend": "841,234",
                    "fee": "2,500", "change": "156,266"},
        "labels": {"inputs": "inputs", "recipients": "recipients",
                   "fee": "fee", "change": "sats change"},
        "button_list": ["Review recipients"],
    })
    assert mod._debug_last_path() == "compiled"

    # address is required on the two detail screens — the native side raises without it.
    with pytest.raises(RuntimeError):
        mod.psbt_address_details_screen({"top_nav": {"title": "Verify Send Address"}})
    with pytest.raises(RuntimeError):
        mod.psbt_change_details_screen({"top_nav": {"title": "Your Change"}})

    # cfg must be a dict.
    with pytest.raises(RuntimeError):
        mod.psbt_overview_screen("not a dict")
    with pytest.raises(RuntimeError):
        mod.psbt_math_screen("not a dict")

    mod.lvgl_shutdown()


def test_callback_driven_topnav_and_ordering():
    mod = _native_or_skip()
    mod.clear_result_queue()

    # Reserved result codes arrive in the index slot (SEEDSIGNER_RET_* in
    # seedsigner.h): 1000 = back, 1001 = power, 1100 = screensaver dismiss.
    # Body buttons emit their 0-based index. label is informational only.
    mod._debug_emit_result("back", 1000)
    mod._debug_emit_result("power", 1001)
    mod._debug_emit_result("screensaver_dismiss", 1100)
    mod._debug_emit_result("First", 1)

    assert mod.poll_for_result() == ("topnav_back", -1, "back")
    assert mod.poll_for_result() == ("topnav_power", -1, "power")
    assert mod.poll_for_result() == ("button_selected", -1, "screensaver_dismiss")
    assert mod.poll_for_result() == ("button_selected", 1, "First")
    assert mod.poll_for_result() is None


def test_validation_errors():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)

    # top_nav.title is required.
    with pytest.raises(RuntimeError):
        mod.button_list_screen({"top_nav": {}, "button_list": ["A"]})

    # Object-form button entries are valid (label + optional icon/color/right_icon),
    # but "label" is required and must be a string.
    with pytest.raises(RuntimeError):
        mod.button_list_screen({"top_nav": {"title": "X"}, "button_list": [{"icon": "A"}]})

    mod.lvgl_shutdown()


def test_object_form_buttons_accepted():
    # Per-button object form (parity with Python ButtonOption: label + optional
    # inline icon, right icon, icon_color, label_color, plus the top-nav icon).
    # Mirrors the screens-side read_button_list_items() contract; exercises the
    # validate_cfg object branch and the native parse/build path headlessly.
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    mod.button_list_screen({
        "top_nav": {"title": "Seed Options", "icon": "", "icon_color": "#ff9900"},
        "button_list": [
            "Plain string still works",
            {"label": "Export Xpub", "icon": ""},
            {"label": "Scan", "right_icon": ""},
            {"label": "Discard", "label_color": "#ff0000"},
        ],
    })
    assert mod._debug_last_path() == "compiled"
    mod.lvgl_shutdown()


def test_locale_pack_discovery_defensive():
    # Discovery over a directory that does not exist is a no-op: an absent packs
    # partition means "no packs", never an error. discover_locale_packs registers
    # nothing (needs no LVGL runtime); list_available_locales returns []. Both take
    # the packs dir explicitly (default "lang-packs").
    mod = _native_or_skip()

    assert mod.discover_locale_packs("/nonexistent/packs/dir") == 0

    # list_available_locales reads the active display profile, so it requires the
    # runtime — before lvgl_init it raises rather than abort()ing the process.
    with pytest.raises(RuntimeError):
        mod.list_available_locales("/nonexistent/packs/dir")

    mod.lvgl_init(hor_res=240, ver_res=240)
    assert mod.list_available_locales("/nonexistent/packs/dir") == []
    mod.lvgl_shutdown()


def test_locale_picker_screen_renders():
    # Live-text rows only (no "image" key ⇒ no endonym-image provider fetch), so the
    # picker builds from the baked floor alone — Español's accented glyphs are all
    # covered. Exercises the cfg→JSON→locale_picker_screen path headlessly.
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    mod.locale_picker_screen({
        "top_nav": {"title": "Language", "show_back_button": True},
        "active_locale": "en",
        "rows": [
            {"locale": "en", "english": "English", "native": "English"},
            {"locale": "es", "english": "Spanish", "native": "Español"},
        ],
    })
    assert mod._debug_last_path() == "compiled"

    # "rows" is required — the native screen raises without it.
    with pytest.raises(RuntimeError):
        mod.locale_picker_screen({"top_nav": {"title": "Language"}})
    # cfg must be a dict.
    with pytest.raises(RuntimeError):
        mod.locale_picker_screen("not a dict")

    mod.lvgl_shutdown()
