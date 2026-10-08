from machine import I2C, Pin
import time
i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=100000)
A = 0x6F
def r8(r): return i2c.readfrom_mem(A, r, 1)[0]
def w8(r, v): i2c.writeto_mem(A, r, bytes([v]))
def bit(rl, pin, v):
    x = r8(rl); w8(rl, (x | (1 << pin)) if v else (x & ~(1 << pin)))
# pin 13 lives in the high-byte registers (bit 5)
bit(0x04, 5, 1); bit(0x0A, 5, 1); bit(0x14, 5, 0)   # output, pull-up, push-pull
w8(0x24, 12)                                        # 12 LEDs
def show(r, g, b, name):
    c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    i2c.writeto_mem(A, 0x30, bytes([c & 0xFF, c >> 8]) * 12)
    w8(0x24, r8(0x24) | 0x40)
    time.sleep_ms(50)
    ram = i2c.readfrom_mem(A, 0x30, 4)
    print('%-5s rgb565=0x%04X  ram[0..1]=%s  cfg=0x%02X' % (name, c, ram.hex(), r8(0x24)))
    time.sleep_ms(900)
for col in ((40, 0, 0, 'red'), (0, 40, 0, 'green'), (0, 0, 40, 'blue'), (0, 0, 0, 'off')):
    show(*col)
