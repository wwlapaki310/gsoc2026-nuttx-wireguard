from machine import I2C, Pin
import time
i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=100000)
def r8(a, r): return i2c.readfrom_mem(a, r, 1)[0]
def r16(a, r): b = i2c.readfrom_mem(a, r, 2); return b[0] << 8 | b[1]
def py_bit(rl, pin, v):
    x = r8(0x6F, rl); x = (x | (1 << pin)) if v else (x & ~(1 << pin)); i2c.writeto_mem(0x6F, rl, bytes([x]))
s0 = r8(0x34, 0x00)
print('AXP2101 status0=0x%02X  VBUS present=%d  battery present=%d' % (s0, (s0 >> 5) & 1, (s0 >> 3) & 1))
print('AW9523 out P0=0x%02X P1=0x%02X  cfg P0=0x%02X P1=0x%02X' % (r8(0x58, 0x02), r8(0x58, 0x03), r8(0x58, 0x04), r8(0x58, 0x05)))
print('INA226 mfr=0x%04X die=0x%04X' % (r16(0x41, 0xFE), r16(0x41, 0xFF)))
def meas(tag):
    time.sleep_ms(300)
    vb = r16(0x41, 0x02) * 1.25 / 1000
    sh = r16(0x41, 0x01); sh = sh - 65536 if sh & 0x8000 else sh
    print('%-12s bus=%.3f V  shunt=%.1f uV' % (tag, vb, sh * 2.5))
py_bit(0x05, 0, 0); meas('VM_EN off')
py_bit(0x03, 0, 1); py_bit(0x09, 0, 1); py_bit(0x05, 0, 1); meas('VM_EN on')
