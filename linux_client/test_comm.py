import serial
import time
import threading

ser = serial.Serial('/dev/ttyACM0', 115200, write_timeout=2)
# ser.setDTR(False)
# ser.setRTS(False)
# time.sleep(0.1)
# ser.setDTR(True)
# ser.setRTS(True)

def read_loop():
    while True:
        try:
            line = ser.readline().decode('ascii', errors='ignore')
            if line:
                print('ESP32: ' + line.strip())
        except Exception as e:
            print("Read error:", e)
            break

t = threading.Thread(target=read_loop, daemon=True)
t.start()
time.sleep(4)

print('Sending CFG...')
try:
    ser.write(b'CFG:SCR=1,BG=FFFFFF,CCPU=0071C5,CGPU=76B900,CRAM=888888\n')
    print('CFG sent!')
except Exception as e:
    print('CFG write error:', e)

time.sleep(1)

print('Sending Stats...')
try:
    ser.write(b'CPU:10,CPUT:50.0,GPU:20,GPUT:60.0,VRAM:2.0/8.0,RAM:4.0/16.0,PWR:55.5,NET:LAN,SPEED:1000 Mbps,DOWN:0.0,UP:0.0\n')
    print('Stats sent!')
except Exception as e:
    print('Stats write error:', e)

time.sleep(2)
print('Done!')
