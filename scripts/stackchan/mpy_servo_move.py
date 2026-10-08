from machine import I2C, Pin, UART
import time
i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=100000)
def r8(r): return i2c.readfrom_mem(0x6F, r, 1)[0]
def py_bit(rl, pin, v):
    x = r8(rl); x = (x | (1 << pin)) if v else (x & ~(1 << pin)); i2c.writeto_mem(0x6F, rl, bytes([x]))
py_bit(0x03, 0, 1); py_bit(0x09, 0, 1); py_bit(0x05, 0, 1); time.sleep_ms(500)
u = UART(1, baudrate=1000000, tx=6, rx=7, timeout=40, rxbuf=512)
def xfer(id, inst, params=b''):
    body = bytes([id, len(params) + 2, inst]) + params
    u.read(); u.write(b'\xff\xff' + body + bytes([(~sum(body)) & 0xFF])); time.sleep_ms(25)
    return u.read() or b''
def pos(id):
    r = xfer(id, 0x02, bytes([0x38, 2]))
    return (r[5] << 8 | r[6]) if len(r) >= 8 and r[2] == id and r[4] == 0 else None
def goto(id, p, t_ms=400):
    # SCSCL goal position block (addr 0x2A): position, time, speed — big-endian
    r = xfer(id, 0x03, bytes([0x2A, p >> 8, p & 0xFF, t_ms >> 8, t_ms & 0xFF, 0, 0]))
    return len(r) >= 6 and r[4] == 0
LIM = {1: (51, 869), 2: (620, 900)}   # BSP zero 460 +/-128 deg (yaw), 620..908 (pitch 0-90 deg)
def safe(id, p):
    lo, hi = LIM[id]; return max(lo, min(hi, p))
start = {1: pos(1), 2: pos(2)}
print('start positions: pan=%s tilt=%s' % (start[1], start[2]))
plan = [(1, 460), (2, 650), (1, 420), (1, 500), (1, 460), (2, 690), (2, 650)]
for sid, target in plan:
    t = safe(sid, target); ok = goto(sid, t); time.sleep_ms(700)
    print('id %d -> %d  ack=%s  read=%s' % (sid, t, ok, pos(sid)))
for sid in (1, 2):
    xfer(sid, 0x03, bytes([0x28, 0]))   # torque off (released)
print('torque released; final: pan=%s tilt=%s' % (pos(1), pos(2)))
