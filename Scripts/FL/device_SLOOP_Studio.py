# name=SLOOP Studio
# url=https://github.com/isod89/sloop-fm1
# SPDX-License-Identifier: GPL-3.0-only
#
# FL Studio controller script for SLOOP Studio: the FM-1's PLAY / STOP start and stop FL.
#
# The SLOOP Studio plugin (with "FM-1 EN VIVO" on and "PLAY FM-1 -> FL" on) watches the FM-1. When
# the FM-1 starts or stops on its own, the plugin sends, on its MIDI port "SLOOP Studio", channel 16:
#   CC 118 = play    CC 119 = stop
# This script turns them into FL's transport. With "SYNC FL" on, FL then sends its clock back to
# the FM-1, which lines up with FL's bar.
#
# Install: this file in  Documents/Image-Line/FL Studio/Settings/Hardware/SLOOP Studio/
# Then FL > Options > MIDI Settings > Input: the port "SLOOP Studio", Controller type "SLOOP Studio",
# Enable. (If the port is not listed: load the plugin first, then "Rescan devices".)

import transport

PLAY_CC = 118
STOP_CC = 119
STATUS = 0xBF   # control change, channel 16


def OnInit():
    print("SLOOP Studio: el PLAY / STOP del FM-1 controla FL")


def OnMidiMsg(event):
    print("SLOOP Studio: recibido", hex(event.status), event.data1, event.data2)
    if event.status != STATUS or event.data1 not in (PLAY_CC, STOP_CC):
        return
    event.handled = True
    playing = bool(transport.isPlaying())
    if event.data1 == PLAY_CC and not playing:
        transport.start()    # (a play / pause toggle: only when FL is stopped)
        print("SLOOP Studio: PLAY del FM-1 -> FL arranca")
    elif event.data1 == STOP_CC and playing:
        transport.stop()
        print("SLOOP Studio: STOP del FM-1 -> FL se detiene")
