from machine import I2C, Pin
import time
# Read the PY32 version register the way the official BSP does: retry every
# 200 ms, version 0x00 / 0xFF means "not running". Bus at 100 kHz.
i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=100000)
print('scan 100k:', ['0x%02X' % a for a in i2c.scan()])
for i in range(10):
    try:
        v = i2c.readfrom_mem(0x6F, 0x02, 1)[0]
        print('try %d: version 0x%02X%s' % (i, v, ' (not running)' if v in (0, 0xFF) else ''))
        if v not in (0, 0xFF):
            break
    except OSError as e:
        print('try %d: error %s' % (i, e))
    time.sleep_ms(200)
try:
    print('raw read (no register):', i2c.readfrom(0x6F, 4).hex())
except OSError as e:
    print('raw read error', e)
