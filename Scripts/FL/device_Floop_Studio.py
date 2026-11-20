# name=Floop Studio
# url=https://github.com/isod89/sloop-fm1
# SPDX-License-Identifier: GPL-3.0-only
#
# FL Studio controller script for Floop Studio: the FM-1's PLAY / STOP start and stop FL.
#
# The Floop Studio plugin (with "FM-1 EN VIVO" on and "PLAY FM-1 -> FL" on) watches the FM-1. When
# the FM-1 starts or stops on its own, the plugin sends, on its MIDI port "Floop Studio", channel 16:
#   CC 118 = play    CC 119 = stop
# This script turns them into FL's transport. With "SYNC FL" on, FL then sends its clock back to
# the FM-1, which lines up with FL's bar.
#
# Install: this file in  <FL user data folder>/FL Studio/Settings/Hardware/Floop Studio/
#   (Options > File settings > User data folder; by default Documents/Image-Line)
# Then FL > Options > MIDI Settings > Input: the port "Floop Studio", Controller type "Floop Studio",
# Enable. (If the port is not listed: load the plugin first, then "Rescan devices".)

import transport

PLAY_CC = 118
STOP_CC = 119
STATUS = 0xBF   # control change, channel 16


def OnInit():
    print("Floop Studio: el PLAY / STOP del FM-1 controla FL")


def OnMidiMsg(event):
    print("Floop Studio: recibido", hex(event.status), event.data1, event.data2)
    if event.status != STATUS or event.data1 not in (PLAY_CC, STOP_CC):
        return
    event.handled = True
    playing = bool(transport.isPlaying())
    if event.data1 == PLAY_CC and not playing:
        transport.start()    # (a play / pause toggle: only when FL is stopped)
        print("Floop Studio: PLAY del FM-1 -> FL arranca")
    elif event.data1 == STOP_CC and playing:
        transport.stop()
        print("Floop Studio: STOP del FM-1 -> FL se detiene")
