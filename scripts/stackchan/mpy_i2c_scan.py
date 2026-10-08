from machine import I2C, Pin
import time
names = {0x34:'AXP2101',0x58:'AW9523B',0x51:'BM8563',0x69:'BMI270',0x40:'ES7210',0x36:'AW88298',0x38:'FT6336',0x6F:'PY32',0x71:'PY32(alt)',0x68:'Si12T',0x21:'GC0308',0x10:'BMM150?',0x40:'ES7210/INA226?'}
for f in (100000, 400000):
    i2c = I2C(0, scl=Pin(11), sda=Pin(12), freq=f)
    time.sleep_ms(50)
    found = i2c.scan()
    print('freq', f, ':', ', '.join('0x%02X(%s)' % (a, names.get(a, '?')) for a in found))
