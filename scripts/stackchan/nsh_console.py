import serial, sys, time
# Open without touching DTR/RTS (on USB-Serial/JTAG they drive reset/boot strap).
port=sys.argv[1]; secs=float(sys.argv[2]); reset='--reset' in sys.argv
cmds=[a for a in sys.argv[3:] if not a.startswith('--')]
s=serial.Serial(); s.port=port; s.baudrate=115200; s.timeout=0.2; s.dtr=False; s.rts=False; s.open()
if reset:
    s.rts=True; time.sleep(0.1); s.rts=False
buf=b''; t0=time.time(); nxt=3.0; i=0
while time.time()-t0<secs:
    buf+=s.read(4096)
    if time.time()-t0>nxt:
        c=cmds[i] if i<len(cmds) else ''
        s.write((c+'\r\n').encode()); i+=1; nxt+=3.0
s.close()
sys.stdout.write(buf.decode('utf-8','replace').replace('\r',''))
