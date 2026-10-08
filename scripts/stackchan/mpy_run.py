import serial, sys, time
# Run a MicroPython script file via raw REPL and print its output.
port, path = sys.argv[1], sys.argv[2]
tmo = float(sys.argv[3]) if len(sys.argv) > 3 else 20
s = serial.Serial(port, 115200, timeout=0.2)
def rd_until(tok, t):
    b = b''; t0 = time.time()
    while time.time() - t0 < t:
        b += s.read(4096)
        if tok in b: break
    return b
s.write(b'\r\x03\x03'); time.sleep(0.3); s.read(65536)
s.write(b'\x01'); rd_until(b'raw REPL; CTRL-B to exit\r\n>', 3)
code = open(path, 'rb').read()
for i in range(0, len(code), 256):
    s.write(code[i:i+256]); time.sleep(0.01)
s.write(b'\x04')
out = rd_until(b'\x04>', tmo)
s.write(b'\x02'); s.close()
out = out.decode('utf-8', 'replace')
if out.startswith('OK'): out = out[2:]
o, _, e = out.partition('\x04')
sys.stdout.write(o)
e = e.split('\x04')[0]
if e.strip(): sys.stdout.write('\n[ERR] ' + e)
