import serial
from time import sleep

ser = serial.Serial('/dev/ttyS0', 9600)
print(f"Connected to: {ser.name}")
print("Running NEW script")  # add this


i = 0
while True:
    ser.write(bytes([i]))
    i = (i + 1) % 256       #MIDI has value 0-127 but MSB will be 0 or 1
    sleep(0.1)              #sleep for 100ms


ser.close()

