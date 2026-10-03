#!/usr/bin/env python3
"""Regain debug access to a target whose firmware quickly makes itself undebuggable
(e.g. enters a low-power mode that gates the core clock).

The debug port and the AHB-AP are prepared (CSW, TAR = DHCSR) while the hardware reset is
held, so right after the reset is released every USB round trip is a halt request. The core
is caught in the boot ROM or early start-up code; then reset vector catch
(DEMCR.VC_CORERESET) is enabled so the next reset by a flashing tool halts the core before
any firmware runs.

    python tools/rescue.py [--uid PROBE_UID] [--hold-ms N]
"""
import argparse
import os
import sys
import time

from pyocd.core.session import Session
from pyocd.probe.aggregator import DebugProbeAggregator
from pyocd.probe.debug_probe import DebugProbe
from pyocd.probe.swj import SWJSequenceSender

DHCSR = 0xE000EDF0
DEMCR = 0xE000EDFC
DCRSR, DCRDR = 0xE000EDF4, 0xE000EDF8
DBGKEY = 0xA05F0000
C_DEBUGEN, C_HALT = 1 << 0, 1 << 1
S_HALT = 1 << 17
VC_CORERESET = 1 << 0
CSW_32BIT = 0x23000002   # 32-bit, no address increment, privileged data access
AP_CSW, AP_TAR, AP_DRW = 0x00, 0x04, 0x0C
DP_ABORT, DP_CTRL, DP_SELECT = 0x0, 0x4, 0x8


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--uid', help='the probe (default: CRTOS_PROBE or the board\'s DAPLink)')
    ap.add_argument('--tries', type=int, default=5)
    ap.add_argument('--clock', type=int, default=4000000)
    ap.add_argument('--hold-ms', type=int, default=20)
    a = ap.parse_args()
    if not a.uid:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import swdcon
        a.uid = swdcon.find_probe()

    probes = DebugProbeAggregator.get_all_connected_probes(unique_id=a.uid)
    if not probes:
        print('no probe')
        return 1
    p = probes[0]
    p.session = Session(None)  # option container only; no target/board is attached
    p.open()
    p.set_clock(a.clock)
    p.connect(DebugProbe.Protocol.SWD)
    swj = SWJSequenceSender(p, True)

    def prepare():
        swj.select_protocol(DebugProbe.Protocol.SWD)   # line reset, JTAG-to-SWD, line reset
        p.read_dp(0x0)                                 # DPIDR
        p.write_dp(DP_ABORT, 0x1E)
        p.write_dp(DP_SELECT, 0x0)                     # AP 0 (AHB-AP), bank 0
        p.write_dp(DP_CTRL, 0x50000000)                # debug + system power-up request
        p.write_ap(AP_CSW, CSW_32BIT)
        p.write_ap(AP_TAR, DHCSR)
        p.read_dp(DP_CTRL)                             # flush

    # line reset (51 ones), JTAG-to-SWD (0xE79E), line reset, 8 idle cycles: one SWJ command
    SWJ_BITS = ((1 << 51) - 1) | (0xE79E << 51) | (((1 << 51) - 1) << 67)
    SWJ_LEN = 126

    def burst():
        """Resync the (just reset) DP and request a halt in two USB round trips."""
        p.swj_sequence(SWJ_LEN, SWJ_BITS)
        dpidr = p.read_dp(0x0, now=False)
        p.write_dp(DP_ABORT, 0x1E)
        p.write_dp(DP_SELECT, 0x0)
        p.write_dp(DP_CTRL, 0x50000000)
        p.write_ap(AP_CSW, CSW_32BIT)
        p.write_ap(AP_TAR, DHCSR)
        p.write_ap(AP_DRW, DBGKEY | C_DEBUGEN | C_HALT)
        v = p.read_ap(AP_DRW, now=False)
        return dpidr(), v()

    def rd(addr):
        p.write_ap(AP_TAR, addr)
        return p.read_ap(AP_DRW)

    def wr(addr, val):
        p.write_ap(AP_TAR, addr)
        p.write_ap(AP_DRW, val)

    for attempt in range(a.tries):
        for _ in range(5):
            try:
                prepare()
                break
            except Exception as e:
                print('prepare:', e)
                time.sleep(0.05)
        p.assert_reset(True)
        time.sleep(a.hold_ms / 1000.0)
        p.assert_reset(False)
        t0 = time.perf_counter()
        trace = []
        for i in range(300):
            ms = (time.perf_counter() - t0) * 1000
            try:
                _, v = burst()
            except Exception as e:
                if len(trace) < 16:
                    trace.append('%.0f:%s' % (ms, type(e).__name__[:6]))
                continue
            if len(trace) < 16:
                trace.append('%.0f:%08x' % (ms, v))
            if v != 0xFFFFFFFF and (v & S_HALT):
                try:
                    wr(DEMCR, rd(DEMCR) | VC_CORERESET)
                    wr(DCRSR, 15)
                    pc = rd(DCRDR)
                    print('halted %.1f ms after reset release (attempt %d), pc=%08x; reset vector catch enabled'
                          % (ms, attempt + 1, pc))
                except Exception as e:
                    print('halted, but setup failed:', e)
                print(' '.join(trace))
                p.close()
                return 0
        print('attempt %d: core did not halt; %s' % (attempt + 1, ' '.join(trace)))
    p.close()
    return 1


if __name__ == '__main__':
    sys.exit(main())
