# Builds reference slots with SLOOP's own sampleio.py, for the C++ port's byte-for-byte test.
import math, sys, struct
sys.path.insert(0, sys.argv[1])
import sampleio as sio
out = sys.argv[2]
def tone(sr, n, f, amp=0.8):
    return [amp * math.sin(2 * math.pi * f * i / sr) * math.exp(-3 * i / n) for i in range(n)]
# instrument: three "files" at different source rates, roots from names
specs = [(44100, 30000, 220.0, "PIANO_A3"), (48000, 20000, 440.0, "PIANO_A4"), (22050, 9000, 880.0, "PIANO_A5")]
zones = []
for sr, n, f, name in specs:
    x = sio.resample(tone(sr, n, f), sr, sio.SLOT_RATE)
    zones.append((sio.to_int16(x), sio.note_from_name(name), None, None))
    open(f"{out}/src_{name}.f64", "wb").write(struct.pack(f"<{n}d", *tone(sr, n, f)))
hdr, data = sio.user_slot("Piano test", zones)
open(f"{out}/inst.hdr", "wb").write(hdr); open(f"{out}/inst.bin", "wb").write(data)
print("inst", len(hdr), len(data), [sio.note_from_name(s[3]) for s in specs], sio.note_from_name("Bb2 pad"), sio.note_from_name("x C#-1"), sio.note_from_name("nada"))
