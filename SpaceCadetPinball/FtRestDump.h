// ---- FTDBG: rest-state dump for the differential harness (test/restdump/) ----
#pragma once

// FT_RESTDUMP=<path>: once a game is running and the first ball has been sitting in
// the plunger lane for FT_RESTDUMP_DELAY seconds of game time (default 2), write
// the state of every component and the table-level rule state to <path> as JSONL,
// then exit. Inert when the variable is unset. The Wine side
// (test/restdump/wine_probe.py) reads the same fields out of the originals.
void ft_restdump_tick(float dtSec);
