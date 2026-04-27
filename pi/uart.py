import serial
from time import sleep
import mido

# Open port to send UART data
ser = serial.Serial('/dev/ttyS0', 9600)
print(f"Connected to: {ser.name}")

# Open Sensel port so we can read MIDI data
port = mido.open_input('Sensel Morph:Sensel Morph MIDI 1 16:0')

# Blocking so will always wait until new data comes in
for msg in port:
    # Filter messages
    if  (msg.type == 'note_on') or \
        (msg.type == 'note_off') or \
        (msg.type == 'control_change'):
                data = bytes(msg.bytes())
                ser.write(data)
                # print(f"Sending: {[hex(b) for b in data]}")  # debug

ser.close()

