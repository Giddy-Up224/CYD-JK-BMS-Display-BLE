# btsnoop_jk.py <btsnoop_hci.log>: lists connections, ATT writes (hex + ASCII) and JK frame starts
# from an Android HCI snoop log. See doc/rev_eng.md.
import struct, sys, datetime

data = open(sys.argv[1], 'rb').read()
assert data[:8] == b'btsnoop\0', 'not a btsnoop file'
ver, link = struct.unpack('>II', data[8:16])
pos = 16
EPOCH = 0x00dcddb30f2f8000  # microseconds from year 0 to 1970
acl = {}  # handle -> bytearray (reassembly)
conn_addr = {}
t0 = None

def att(t, sent, handle, p):
    op = p[0]
    who = '->' if sent else '<-'
    ts = datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime('%H:%M:%S.%f')[:-3]
    if op in (0x52, 0x12):
        h = struct.unpack('<H', p[1:3])[0]
        v = p[3:]
        print(f'{ts} {who} conn {handle:#05x} WRITE{"_REQ" if op == 0x12 else ""} h={h:#06x} len={len(v)}: {v.hex(" ")}')
        txt = bytes(c if 32 <= c < 127 else 46 for c in v)
        print(f'{"":13}      ascii: {txt.decode()}')
    elif op == 0x1b:
        v = p[3:]
        if v[:4] == b'\x55\xaa\xeb\x90':
            print(f'{ts} {who} conn {handle:#05x} NOTIFY frame type {v[4]:#04x} ({len(v)} bytes first part)')

while pos + 24 <= len(data):
    olen, ilen, flags, drops, ts = struct.unpack('>IIIIq', data[pos:pos + 24])
    pkt = data[pos + 24:pos + 24 + ilen]
    pos += 24 + ilen
    t = (ts - EPOCH) / 1e6
    sent = (flags & 1) == 0
    if link == 1002:
        if not pkt or pkt[0] != 0x02:
            # LE connection complete events: remember peer addresses
            if pkt and pkt[0] == 0x04 and len(pkt) > 3 and pkt[1] == 0x3e and pkt[3] in (0x01, 0x0a):
                h = struct.unpack('<H', pkt[5:7])[0]
                addr = ':'.join(f'{b:02X}' for b in pkt[9:15][::-1])
                print(f'-- connection {h:#05x} to {addr}')
            continue
        pkt = pkt[1:]
    hdr, ln = struct.unpack('<HH', pkt[:4])
    handle, pb = hdr & 0x0fff, (hdr >> 12) & 0x3
    body = pkt[4:4 + ln]
    if pb in (0, 2):
        acl[handle] = bytearray(body)
    else:
        acl.setdefault(handle, bytearray()).extend(body)
    buf = acl[handle]
    if len(buf) < 4:
        continue
    l2len, cid = struct.unpack('<HH', buf[:4])
    if len(buf) < 4 + l2len:
        continue
    if cid == 0x0004:
        att(t, sent, handle, bytes(buf[4:4 + l2len]))
    acl[handle] = bytearray()
