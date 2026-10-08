from machine import I2C, Pin, UART
import time
i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=100000)
def r8(r): return i2c.readfrom_mem(0x6F, r, 1)[0]
def py_bit(rl, pin, v):
    x = r8(rl); x = (x | (1 << pin)) if v else (x & ~(1 << pin)); i2c.writeto_mem(0x6F, rl, bytes([x]))
py_bit(0x03, 0, 1); py_bit(0x09, 0, 1); py_bit(0x05, 0, 1); time.sleep_ms(500)
print('PY32 out L=0x%02X dir L=0x%02X' % (r8(0x05), r8(0x03)))
def pkt(id, inst, params=b''):
    body = bytes([id, len(params) + 2, inst]) + params
    return b'\xff\xff' + body + bytes([(~sum(body)) & 0xFF])
for baud in (1000000, 500000, 115200):
    u = UART(1, baudrate=baud, tx=6, rx=7, timeout=40, rxbuf=512)
    hits = []
    for sid in list(range(0, 21)) + [0xFE]:
        u.read(); p = pkt(sid, 0x01); u.write(p); time.sleep_ms(25)
        r = u.read()
        if r: hits.append('%d:%s' % (sid, r.hex()))
    print('baud', baud, 'replies:', hits if hits else 'none')
    u.deinit()
# loopback sanity: is anything at all visible on RX while we transmit?
u = UART(1, baudrate=1000000, tx=6, rx=7, timeout=40)
u.write(b'\x55' * 32); time.sleep_ms(10); print('RX while TX (echo test):', u.read())
print('GPIO7 idle level:', Pin(7, Pin.IN).value())
