from machine import I2C, Pin, SPI
import time
i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=100000)
def bit_on(a, r, m): i2c.writeto_mem(a, r, bytes([i2c.readfrom_mem(a, r, 1)[0] | m]))
# power: AXP2101 DLDO1 (backlight) on, brightness 24 (range 20..28 per M5GFX)
bit_on(0x34, 0x90, 0x80); i2c.writeto_mem(0x34, 0x99, bytes([24]))
print('AXP 0x90=0x%02X 0x99=%d  AW9523 P1=0x%02X (bit1 = LCD RST)' % (
    i2c.readfrom_mem(0x34, 0x90, 1)[0], i2c.readfrom_mem(0x34, 0x99, 1)[0], i2c.readfrom_mem(0x58, 0x03, 1)[0]))
CS, SCK, MOSI, DC = Pin(3, Pin.OUT, value=1), Pin(36, Pin.OUT, value=0), Pin(37, Pin.OUT, value=0), Pin(35, Pin.OUT, value=1)
def tx(b):
    for i in range(7, -1, -1):
        MOSI.value((b >> i) & 1); SCK.value(1); SCK.value(0)
def wcmd(cmd, data=b''):
    CS.value(0); DC.value(0); tx(cmd); DC.value(1)
    for d in data: tx(d)
    CS.value(1)
def rcmd(cmd, nbits, dummy=1):
    CS.value(0); DC.value(0); tx(cmd)
    DC.value(1); MOSI.init(Pin.IN)       # 3-wire SPI: the panel answers on SDA (GPIO37)
    for _ in range(dummy): SCK.value(1); SCK.value(0)
    v = 0
    for _ in range(nbits):
        SCK.value(1); v = (v << 1) | MOSI.value(); SCK.value(0)
    CS.value(1); MOSI.init(Pin.OUT, value=0)
    return v
print('RDDID(04h) = 0x%06X' % rcmd(0x04, 24))
# variant probe (M5GFX _probe_ili9342_variant, 8-bit reads with one dummy bit)
def rparam(cmd, idx):
    wcmd(0xD9, bytes([0x10 | idx])); return rcmd(cmd, 8, 1)
wcmd(0xD9, b'\x00'); wcmd(0xDD, b'\x01'); wcmd(0xCB, b'\x1c')
k0, k1 = rparam(0xDD, 1), rparam(0xCB, 1); wcmd(0xD9, b'\x00')
if (k0, k1) == (0x01, 0x1C):
    variant = 'ILI9342E'
else:
    wcmd(0xC8, b'\xff\x93\x42'); k2, k3 = rparam(0xD3, 2), rparam(0xD3, 3); wcmd(0xD9, b'\x00')
    variant = 'ILI9342C' if (k2 & 0x7F, k3) == (0x13, 0x42) or (k2, k3) == (0x93, 0x42) else 'unknown(C assumed)'
    print('ID4 keys: %02X %02X' % (k2, k3))
print('variant keys DDh=%02X CBh=%02X -> %s' % (k0, k1, variant))
# minimal MIPI-DCS init, then fill with hardware SPI
wcmd(0x11); time.sleep_ms(120)
wcmd(0x3A, b'\x55'); wcmd(0x36, b'\x08'); wcmd(0x21); wcmd(0x29)
spi = SPI(2, baudrate=20000000, sck=Pin(36), mosi=Pin(37), miso=None)
def window(x0, y0, x1, y1):
    for c, a, b in ((0x2A, x0, x1), (0x2B, y0, y1)):
        CS.value(0); DC.value(0); spi.write(bytes([c])); DC.value(1); spi.write(bytes([a >> 8, a & 255, b >> 8, b & 255])); CS.value(1)
def fill(x0, y0, x1, y1, c565):
    window(x0, y0, x1, y1)
    CS.value(0); DC.value(0); spi.write(b'\x2c'); DC.value(1)
    line = bytes([c565 >> 8, c565 & 255]) * (x1 - x0 + 1)
    for _ in range(y1 - y0 + 1): spi.write(line)
    CS.value(1)
W, H = 320, 240
fill(0, 0, W - 1, H - 1, 0x0000)                      # black
fill(70, 70, 110, 110, 0xFFFF); fill(210, 70, 250, 110, 0xFFFF)   # eyes
fill(130, 170, 190, 180, 0xFFFF)                       # mouth
fill(0, 0, 9, 9, 0xF800)                               # red marker at (0,0) for readback
spi.deinit(); SCK.init(Pin.OUT, value=0); MOSI.init(Pin.OUT, value=0)
def rpix(x, y):
    window_bb = lambda c, a, b: wcmd(c, bytes([a >> 8, a & 255, b >> 8, b & 255]))
    window_bb(0x2A, x, x); window_bb(0x2B, y, y)
    return rcmd(0x2E, 24, 9)    # RAMRD: 8 dummy clocks + 1 dummy bit, then RGB666 (3 bytes)
for name, xy in (('marker(0,0)', (2, 2)), ('eye', (90, 90)), ('background', (160, 30)), ('mouth', (160, 175))):
    print('pixel %-12s %-10s = 0x%06X' % (name, xy, rpix(*xy)))
