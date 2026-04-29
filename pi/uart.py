import serial
from time import sleep
import mido
from gpiozero import LED

#let the MIDI device power up and connect before doing anything
#indicator that program is running
led = LED(20)
for i in range(5): 
    led.on()
    sleep(0.5)
    led.off()
    sleep(0.5)


# Open port to send UART data
ser = serial.Serial('/dev/ttyS0', 9600)
# print(f"Connected to: {ser.name}")

# Open Sensel port so we can read MIDI data
#run {python3 -c "import mido; print(mido.get_input_names())"} in CML
# port = mido.open_input('Sensel Morph:Sensel Morph MIDI 1 20:0')

port = None
while port is None:
    try:
        print("Opening MIDI port...")
        port = mido.open_input(next(p for p in mido.get_input_names() if 'Sensel' in p))
        print("MIDI port opened!")
    except (OSError, StopIteration):
        print("Waiting for Sensel...")
        sleep(2)

#indicator that MIDI port is connected
led.on()

# Blocking so will always wait until new data comes in
for msg in port:
    # Filter messages
    if  (msg.type == 'note_on') or \
        (msg.type == 'note_off') or \
        (msg.type == 'control_change'):
                data = bytes(msg.bytes())
                ser.write(data)
                # print(f"Sending: {[hex(b) for b in data]}")  # debug

#NOTE: add an led so the user knows when the pi starts reading data

ser.close()

