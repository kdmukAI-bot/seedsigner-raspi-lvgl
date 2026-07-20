#!/usr/bin/env python3
"""Interactive i18n locale tester for the Pi Zero (ST7789 + joystick/buttons).

Pick a language from the menu, view a localized demo screen rendered by the
shared LVGL i18n font packs, navigate it with the joystick, then BACK to the
menu to switch languages. Exercises every pack TYPE on real hardware: the
baked Western floor, the Cyrillic / CJK / Farsi packs, and the Devanagari /
Thai / Nastaliq complex-script glyph-run packs.

Run on the Pi with the seedsigner service stopped (it owns the display/GPIO):

    sudo systemctl stop seedsigner.service
    PYTHONPATH=/home/pi/seedsigner-raspi-lvgl/src \\
        python3 /home/pi/seedsigner-raspi-lvgl/tests/pi_i18n_locale_test.py
    sudo systemctl start seedsigner.service

or just run tests/run_pi_i18n_test.sh, which stops/restarts the service for you.

Controls:
  - joystick up/down : move focus between list items
  - center click     : select the focused item
  - BACK arrow        : (on a demo screen) return to the language menu
  - POWER icon        : (on the language menu) exit the tester
  - Ctrl+C            : exit at any time
"""
from __future__ import annotations

import sys
from pathlib import Path

SRC = Path("/home/pi/seedsigner-raspi-lvgl/src")
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import seedsigner_lvgl_screens as lv

# Font packs deploy beside the .so (see dev-device-deployment.md). The native
# set_locale takes the dir explicitly now that the seedsigner_lvgl facade — which
# used to default it to DEFAULT_FONT_DIR — has been removed.
LANG_PACKS = str(SRC / "lang-packs")

# lvgl_pump cycle (ms) before returning to Python — shorter = snappier Ctrl+C.
PUMP_MS = 300

# (locale_id, menu_label [ASCII, always legible], demo_title, [demo buttons]).
# The demo titles/buttons are real corpus strings confirmed to render on-device;
# the complex-script ones (hi/th/ur) are exact runs.bin-keyed text.
LOCALES = [
    ("en",         "en  English",              "Settings",   ["Single Sig", "Multisig", "Passphrase"]),
    ("ru",         "ru  Russian (Cyrillic)",   "Настройки",  ["Подпись", "Кошелёк", "Сид-фраза"]),
    ("zh_Hans_CN", "zh  Chinese (CJK)",        "设置",        ["单签", "多签", "设置"]),
    ("fa",         "fa  Farsi (RTL)",          "تنظیمات",    ["امضا", "کیف پول", "عبارت عبور"]),
    ("hi",         "hi  Hindi (Devanagari)",   "1 इनपुट",    ["12 शब्द", "12वां शब्द", "24 शब्द"]),
    ("th",         "th  Thai",                 "1 อินพุต",   ["12 คำ", "คำที่ 12", "แบบ 12 คำ"]),
    ("ur",         "ur  Urdu (Nastaliq, RTL)", "احتیاط",     ["ٹھیک ہے", "اسکین کریں", "میں سمجھ گیا"]),
]


def pump_until_result():
    """Pump LVGL until a result event arrives; return the (kind, index, label) tuple."""
    while True:
        lv.lvgl_pump(duration_ms=PUMP_MS)
        ev = lv.poll_for_result()
        if ev is not None:
            return ev


def show_list(title, buttons, *, show_back, show_power):
    lv.clear_result_queue()
    cfg = {
        "top_nav": {"title": title, "show_back_button": show_back, "show_power_button": show_power},
        "button_list": buttons,
    }
    lv.button_list_screen(cfg)  # pure builder: returns immediately, we pump from Python
    return pump_until_result()


def main() -> int:
    sys.stdout.reconfigure(line_buffering=True)  # flush per line (visible when piped/logged)
    lv.lvgl_init(hor_res=240, ver_res=240)
    lv.native_display_init()             # also initializes the joystick/button GPIO
    lv.set_flush_mode("native")
    print("[i18n-test] ready. Menu: click a language. On a demo: BACK returns. "
          "On the menu: POWER exits. Ctrl+C quits.")
    try:
        while True:
            # Language menu — always rendered in English so the menu itself is
            # always legible regardless of the previously selected script.
            lv.set_locale("en", LANG_PACKS)
            ev = show_list("Language", [row[1] for row in LOCALES],
                           show_back=False, show_power=True)
            # Power is a button_selected sentinel (index 1001), not a distinct kind —
            # matches the ESP binding. Test it BEFORE indexing LOCALES with ev[1].
            if ev[0] == "button_selected" and ev[1] == 1001:  # RET_CODE__POWER_BUTTON
                break
            if ev[0] != "button_selected":
                continue

            loc, label, title, buttons = LOCALES[ev[1]]
            ok = lv.set_locale(loc, LANG_PACKS)  # font packs deployed beside the .so
            print(f"[i18n-test] -> {label.strip()}   set_locale({loc!r})={ok}")

            # Demo screen in the chosen locale; loop until BACK returns to the menu.
            while True:
                ev2 = show_list(title, buttons, show_back=True, show_power=False)
                if ev2[0] == "button_selected" and ev2[1] == 1000:  # RET_CODE__BACK_BUTTON
                    break
                print(f"[i18n-test]    selected: {ev2}")
    except KeyboardInterrupt:
        print("\n[i18n-test] interrupted.")
    finally:
        lv.native_display_shutdown()
        lv.lvgl_shutdown()
        print("[i18n-test] done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
