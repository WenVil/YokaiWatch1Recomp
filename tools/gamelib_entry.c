// Entry of the game code library (runtime/gamelib.h), compiled into it with the generated code: the function
// tables, found by name when the app loads the library. Everything else in it stays hidden.
#include "gamelib.h"

extern const uint64_t main_fn_addrs[], sdk_fn_addrs[];
extern const uint64_t main_fn_count, sdk_fn_count;
extern const GuestFn main_fn_ptrs[], sdk_fn_ptrs[];

#ifdef NO_SUBSDK0
static const uint64_t subsdk0_fn_addrs[] = {0};
static const uint64_t subsdk0_fn_count = 0;
static const GuestFn subsdk0_fn_ptrs[] = {0};
#else
extern const uint64_t subsdk0_fn_addrs[];
extern const uint64_t subsdk0_fn_count;
extern const GuestFn subsdk0_fn_ptrs[];
#endif

GAMELIB_EXPORT const GameLibTables *lasgame_tables(void) {
    static const GameLibTables t = {GAMELIB_ABI,  GAMELIB_ID,   main_fn_addrs, sdk_fn_addrs,
                                    subsdk0_fn_addrs, main_fn_ptrs, sdk_fn_ptrs, subsdk0_fn_ptrs,
                                    &main_fn_count, &sdk_fn_count, &subsdk0_fn_count};
    return &t;
}
