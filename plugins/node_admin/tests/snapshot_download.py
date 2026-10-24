#!/usr/bin/env python3
"""Live streaming regression; acceptance.sh supplies a disposable node and directory."""
import hashlib
import http.client
import json
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit

base, token, directory, pid = sys.argv[1:]
url = urlsplit(base)
headers = {'Authorization': 'Bearer ' + token}
large = Path(directory) / 'streaming-regression.vizjson'
empty = Path(directory) / 'streaming-empty.vizjson'
size = 64 * 1024 * 1024 + 123
clients = []

def connect():
    return http.client.HTTPConnection(url.hostname, url.port, timeout=5)

def rss():
    for line in Path('/proc/' + pid + '/status').read_text().splitlines():
        if line.startswith('VmRSS:'):
            return int(line.split()[1]) * 1024
    raise AssertionError('node RSS unavailable')

def ping():
    c = connect()
    try:
        start = time.monotonic()
        c.request('POST', '/', json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'admin.ping'}),
                  dict(headers, **{'Content-Type': 'application/json'}))
        response = c.getresponse()
        assert response.status == 200
        assert json.loads(response.read())['result']['ok']
        assert time.monotonic() - start < 2, 'download blocked the admin listener'
    finally:
        c.close()

try:
    block = bytes(range(256)) * 4096
    expected = hashlib.sha256()
    with large.open('wb') as output:
        remaining = size
        while remaining:
            part = block[:min(len(block), remaining)]
            output.write(part)
            expected.update(part)
            remaining -= len(part)
    empty.write_bytes(b'')
    c = connect()
    c.request('HEAD', '/snapshot/' + large.name, headers=headers)
    response = c.getresponse()
    assert response.status == 200 and int(response.getheader('Content-Length')) == size
    assert response.read() == b''
    c.close()
    c = connect()
    c.request('GET', '/snapshot/' + large.name, headers=headers)
    response = c.getresponse()
    assert response.status == 200 and int(response.getheader('Content-Length')) == size
    received = 0
    digest = hashlib.sha256()
    while True:
        chunk = response.read(64 * 1024)
        if not chunk:
            break
        received += len(chunk)
        digest.update(chunk)
    c.close()
    assert received == size and digest.digest() == expected.digest(), 'truncated/corrupted download'
    print('ok   snapshot 64 MiB + 123 bytes: HEAD length and GET SHA-256 match')

    before = rss()
    for _ in range(2):
        c = connect()
        c.connect()
        import socket
        c.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
        c.request('GET', '/snapshot/' + large.name, headers=headers)
        response = c.getresponse()
        assert response.status == 200 and len(response.read(1)) == 1
        clients.append((c, response))
    time.sleep(.5)
    for _ in range(5):
        ping()
    assert rss() - before < 24 * 1024 * 1024, 'snapshot bodies buffered in node memory'
    print('ok   two stalled large downloads keep admin responsive and bounded memory')
    # Leave one receiver stalled long enough to exercise the per-write timeout.
    clients[1][1].close()
    clients[1][0].close()
    clients.pop()
    time.sleep(31)
    stalled, response = clients.pop()
    partial = 1
    try:
        while True:
            chunk = response.read(64 * 1024)
            if not chunk:
                break
            partial += len(chunk)
    except ConnectionResetError:
        pass # Timed-out transfers abort the socket instead of flushing a queued body.
    response.close()
    stalled.close()
    assert partial < size, 'stalled transfer did not time out'
    print('ok   stalled snapshot socket times out without stopping the listener')
    # Dropped clients must release file/socket state and leave the listener usable.
    time.sleep(.2)
    ping()
    c = connect()
    c.request('GET', '/snapshot/' + empty.name, headers=headers)
    response = c.getresponse()
    assert response.status == 200 and response.getheader('Content-Length') == '0'
    assert response.read() == b''
    c.close()
    print('ok   disconnect recovery and empty snapshot download')
finally:
    for c, response in clients:
        response.close()
        c.close()
    large.unlink(missing_ok=True)
    empty.unlink(missing_ok=True)
