// The supported game versions, by the main program's build ID (exefs/main, 0x40..0x53). An unknown build ID is
// refused: there is no fallback table. tools/game_addresses.py reads this same file.
// An X-macro list (no include guard): GA_VERSION(key, label, main build ID) and
// GA_RELEASE(key, label, SHA-256 of exefs/main.npdm).

GA_VERSION(vyw1_jp_v1_3_0, "Yo-kai Watch 1 JP 1.3.0", "B6E172353E696E656A538CEBD5722661C89B368F")
