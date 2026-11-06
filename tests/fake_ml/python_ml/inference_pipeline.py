"""
Test fixture standing in for the real torch pipeline (use with CAISSA_ROOT=tests/fake_ml).
Lets tests/uci_smoke.py exercise the C++<->Python bridge (GIL, threads, lazy loading,
the ML switch) without needing torch. Records what happened in $FAKE_ML_DIR.
"""
import os, threading, time

LOG_DIR = os.environ.get("FAKE_ML_DIR", ".")
open(os.path.join(LOG_DIR, "imported"), "w").write("1")           # proves the module was loaded at all


def get_search_priors(fen):
    with open(os.path.join(LOG_DIR, "calls"), "a") as f:
        f.write(threading.current_thread().name + "\n")           # which thread called us
    time.sleep(0.2)                                               # pretend inference costs 200 ms
    p = [0.0] * 4096
    p[6 * 64 + 21] = 0.6                                          # g1f3
    p[12 * 64 + 28] = 0.4                                         # e2e4
    return p
