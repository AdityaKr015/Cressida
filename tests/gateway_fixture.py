"""A synthetic loopback WebSocket peer. No account, browser, or external network."""
import base64
import hashlib
import json
import socket
import struct
import subprocess
import sys
import threading
import time


def exact(connection, size):
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise EOFError("client closed WebSocket")
        data += chunk
    return data


def receive(connection):
    first, second = exact(connection, 2)
    size = second & 127
    if size == 126:
        size = struct.unpack("!H", exact(connection, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(connection, 8))[0]
    assert second & 128 and size < 65536, "client frames must be masked and bounded"
    mask = exact(connection, 4)
    data = exact(connection, size)
    data = bytes(byte ^ mask[index % 4] for index, byte in enumerate(data))
    return json.loads(data)


def frame(connection, data, opcode=1, final=True):
    if isinstance(data, dict):
        data = json.dumps(data, ensure_ascii=False).encode()
    header = bytes([(128 if final else 0) | opcode])
    header += bytes([len(data)]) if len(data) < 126 else b"\x7e" + struct.pack("!H", len(data))
    connection.sendall(header + data)


def packet(connection, op, data, kind=None, sequence=None):
    frame(connection, {"op": op, "d": data, "t": kind, "s": sequence})


def upgrade(listener, path="/?v=9&encoding=json"):
    connection, _ = listener.accept()
    connection.settimeout(4)
    request = b""
    while b"\r\n\r\n" not in request:
        request += exact(connection, 1)
    assert request.split(b"\r\n", 1)[0] == f"GET {path} HTTP/1.1".encode(), "wrong Gateway route"
    headers = dict(line.split(b": ", 1) for line in request.split(b"\r\n")[1:] if b": " in line)
    key = next(value for name, value in headers.items() if name.lower() == b"sec-websocket-key")
    accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
    packet(connection, 10, {"heartbeat_interval": 1000})
    return connection


def wait_for(connection, wanted):
    for _ in range(12):
        value = receive(connection)
        if value["op"] == 1:
            packet(connection, 11, None)
        if value["op"] == wanted:
            return value
    raise AssertionError("expected Gateway opcode was not received")


def presence_restore(listener, phase):
    with upgrade(listener) as connection:
        wait_for(connection, 2)
        session = f"presence-{phase}"
        status = ["dnd", "idle", "online"][phase]
        packet(connection, 0, {"session_id": session, "sessions": [{"session_id": session, "status": status}]}, "READY", 1)
        presence = wait_for(connection, 3)
        assert presence["d"]["status"] == status, "READY status must win over the settings read"
        if phase == 0:
            assert presence["d"]["activities"] == [{"name": "Custom Status", "type": 4, "state": "early custom status", "emoji": {"id": "77", "name": "wave"}}]
        else:
            assert presence["d"]["activities"] == []
        wait_for(connection, 37)
        if phase:
            packet(connection, 0, {}, "RESTORE_PRESENCE", 2)
            if phase == 1:
                # A late restore changes only presence, without repeating the
                # subscription or reconnecting an already established socket.
                while True:
                    presence = receive(connection)
                    if presence["op"] == 1:
                        packet(connection, 11, None)
                        continue
                    assert presence["op"] == 3, "presence restoration changed the subscription"
                    break
                assert presence["d"]["status"] == "idle"
                assert presence["d"]["activities"] == [{"name": "Custom Status", "type": 4, "state": "late custom status", "emoji": {"name": "🌙"}}]
        packet(connection, 0, {}, "RESTORE_COMPLETE", 3)


def serve(listener, errors):
    try:
        for phase in range(3):
            presence_restore(listener, phase)
        with upgrade(listener) as connection:
            identify = wait_for(connection, 2)
            assert identify["d"]["token"] == "synthetic-token"
            assert identify["d"]["compress"] is False
            resume_url = f"ws://127.0.0.1:{listener.getsockname()[1]}/resume"
            packet(connection, 0, {"session_id": "synthetic-session", "resume_gateway_url": resume_url, "relationships": [{"id": "7", "nickname": "Initial"}]}, "READY", 1)
            presence = wait_for(connection, 3)
            assert presence["d"]["status"] == "dnd"
            assert presence["d"]["activities"] == [{"name": "Custom Status", "type": 4, "state": "working", "emoji": {"id": "77", "name": "wave"}}]
            subscription = wait_for(connection, 37)
            assert subscription["d"]["subscriptions"]["1"]["channels"]["2"] == [[100, 199], [200, 299]]
            members = wait_for(connection, 8)
            assert members["d"] == {"guild_id": "1", "user_ids": ["7", "8"], "limit": 100}
            packet(connection, 0, {"guild_id": "1", "members": [{"user": {"id": "7"}, "roles": ["50"]}, {"user": {"id": "8"}, "roles": []}], "chunk_index": 0, "chunk_count": 1}, "GUILD_MEMBERS_CHUNK", 2)
            search = wait_for(connection, 8)
            assert search["d"] == {"guild_id": "1", "query": "May", "limit": 25, "presences": False, "nonce": "complete-test"}
            packet(connection, 0, {"guild_id": "1", "nonce": "complete-test", "members": [{"user": {"id": "9", "username": "maya"}, "nick": "May", "roles": []}], "chunk_index": 0, "chunk_count": 1}, "GUILD_MEMBERS_CHUNK", 2)
            packet(connection, 0, {"merged_presences": {"friends": [{"user_id": "7", "status": "online"}]}}, "READY_SUPPLEMENTAL", 3)
            packet(connection, 0, {"user": {"id": "7"}, "status": "idle"}, "PRESENCE_UPDATE", 4)
            packet(connection, 0, {"id": "7", "nickname": "My friend"}, "RELATIONSHIP_UPDATE", 5)
            # Control frames may occur between fragments of a text message.
            data = json.dumps({"op": 0, "d": {"content": "synthetic 🌿"}, "t": "MESSAGE_CREATE", "s": 6}, ensure_ascii=False).encode()
            frame(connection, data[:17], final=False)
            frame(connection, b"ping", opcode=9)
            frame(connection, data[17:], opcode=0)
            packet(connection, 0, {}, "MESSAGE_UPDATE", 7)
            packet(connection, 0, {}, "GUILD_MEMBER_LIST_UPDATE", 8)
            reconnect_at = time.monotonic()
            packet(connection, 7, None)
        # Explicit Gateway handoffs should not incur transport-failure backoff.
        # The original unconditional backoff waits at least one second here.
        listener.settimeout(0.85)
        with upgrade(listener, "/resume?v=9&encoding=json") as connection:
            assert time.monotonic() - reconnect_at < 0.85, "Gateway delayed an explicit reconnect"
            resume = wait_for(connection, 6)
            assert resume["d"] == {"token": "synthetic-token", "session_id": "synthetic-session", "seq": 8}
            packet(connection, 0, {}, "RESUMED", 9)
            presence = wait_for(connection, 3)
            assert presence["d"]["status"] == "dnd" and presence["d"]["activities"][0]["state"] == "working"
            wait_for(connection, 37)
            packet(connection, 0, {"id": "7"}, "RELATIONSHIP_REMOVE", 10)
            packet(connection, 0, {}, "MESSAGE_DELETE", 11)
            frame(connection, struct.pack("!H", 4004), opcode=8)
    except Exception as error:
        errors.append(error)


with socket.socket() as listener:
    listener.bind(("127.0.0.1", 0))
    listener.listen(2)
    listener.settimeout(8)
    errors = []
    thread = threading.Thread(target=serve, args=(listener, errors), daemon=True)
    thread.start()
    result = subprocess.run([sys.argv[1], f"ws://127.0.0.1:{listener.getsockname()[1]}/?v=9&encoding=json"], timeout=10)
    thread.join(timeout=1)
    if errors:
        raise errors[0]
    sys.exit(result.returncode)
