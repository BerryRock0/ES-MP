#!/usr/bin/env python3
"""Functional test for the ES-MP LAN server.

Speaks the NetworkProtocol wire format directly so it exercises the real
server binary end to end:
  * login accepted with the correct password
  * login rejected with the wrong password
  * duplicate nickname rejected
  * chat relayed between players
  * snapshots broadcast to every player
"""
import socket
import struct
import subprocess
import sys
import time

VERSION = 1
MAX_NAME = 32
MAX_PASS = 64
MAX_REASON = 128
MAX_CHAT = 256

LOGIN_REQUEST = 1
LOGIN_ACCEPTED = 2
LOGIN_REJECTED = 3
PLAYER_INPUT = 4
SNAPSHOT = 5
CHAT_MESSAGE = 6
CHAT_SEND = 7
DISCONNECT = 8
SERVER_SHUTDOWN = 9

PORT = 47699
PASSWORD = "hunter2"


def frame(msg_type, payload):
    body = bytes([msg_type]) + payload
    return struct.pack("<I", len(body)) + body


def login_request(nick, password):
    name = nick.encode()[:MAX_NAME - 1].ljust(MAX_NAME, b"\0")
    pw = password.encode()[:MAX_PASS - 1].ljust(MAX_PASS, b"\0")
    return frame(LOGIN_REQUEST, struct.pack("<H", VERSION) + name + pw)


def chat_send(text, sender=""):
    # ChatSend carries a full ChatMessage; the server overwrites the sender
    # fields with the authenticated identity before relaying.
    payload = struct.pack("<I", 0)
    payload += sender.encode()[:MAX_NAME - 1].ljust(MAX_NAME, b"\0")
    payload += text.encode()[:MAX_CHAT - 1].ljust(MAX_CHAT, b"\0")
    return frame(CHAT_SEND, payload)


def input_state(buttons, thrust, turn):
    return frame(PLAYER_INPUT, struct.pack("<Iff", buttons, thrust, turn))


class Client:
    def __init__(self, nick, password):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=5)
        self.sock.setblocking(False)
        self.buf = b""
        self.nick = nick
        self.sock.sendall(login_request(nick, password))

    def _pump(self):
        """Read whatever is available into self.buf. Returns False if closed."""
        try:
            data = self.sock.recv(65536)
            if not data:
                return False
            self.buf += data
        except BlockingIOError:
            pass
        except ConnectionResetError:
            return False
        return True

    def drain(self, seconds=0.4):
        """Discard everything currently buffered."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            self._pump()
            time.sleep(0.01)
        self.buf = b""

    def read_messages(self, timeout=2.0):
        """Return a list of (type, payload) tuples received within timeout."""
        msgs = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not self._pump():
                break
            while len(self.buf) >= 4:
                length = struct.unpack("<I", self.buf[:4])[0]
                if len(self.buf) < 4 + length:
                    break
                body = self.buf[4:4 + length]
                self.buf = self.buf[4 + length:]
                msgs.append((body[0], body[1:]))
            time.sleep(0.01)
        return msgs

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def parse_login_response(payload):
    player_id = struct.unpack("<I", payload[:4])[0]
    reason = payload[4:4 + MAX_REASON].split(b"\0", 1)[0].decode(errors="replace")
    return player_id, reason


def parse_chat(payload):
    sender_id = struct.unpack("<I", payload[:4])[0]
    sender = payload[4:4 + MAX_NAME].split(b"\0", 1)[0].decode(errors="replace")
    text = payload[4 + MAX_NAME:4 + MAX_NAME + MAX_CHAT].split(b"\0", 1)[0].decode(errors="replace")
    return sender_id, sender, text


def parse_snapshot(payload):
    tick, count = struct.unpack("<II", payload[:8])
    ships = []
    off = 8
    for _ in range(count):
        sid = struct.unpack("<I", payload[off:off + 4])[0]
        x, y, vx, vy, ang = struct.unpack("<ddddd", payload[off + 4:off + 44])
        hull = struct.unpack("<i", payload[off + 44:off + 48])[0]
        ships.append((sid, x, y, hull))
        off += 48
    return tick, ships


def wait_for(msgs, msg_type):
    for t, p in msgs:
        if t == msg_type:
            return p
    return None


def last_of(msgs, msg_type):
    result = None
    for t, p in msgs:
        if t == msg_type:
            result = p
    return result


def main():
    server = subprocess.Popen(
        ["/tmp/esmp-server", "--port", str(PORT), "--password", PASSWORD, "--name", "TestHost"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.4)
    failures = []

    def check(name, cond, detail=""):
        status = "PASS" if cond else "FAIL"
        print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
        if not cond:
            failures.append(name)

    try:
        # 1. Correct password -> accepted.
        a = Client("Alice", PASSWORD)
        msgs = a.read_messages(1.0)
        resp = wait_for(msgs, LOGIN_ACCEPTED)
        check("login accepted with correct password", resp is not None,
              f"got types {sorted(set(t for t, _ in msgs))}")
        alice_id = parse_login_response(resp)[0] if resp else 0

        # 2. Wrong password -> rejected.
        b = Client("Bob", "wrongpass")
        msgs = b.read_messages(1.0)
        rej = wait_for(msgs, LOGIN_REJECTED)
        check("login rejected with wrong password", rej is not None,
              f"got types {sorted(set(t for t, _ in msgs))}")
        if rej:
            check("rejection carries a reason", len(parse_login_response(rej)[1]) > 0,
                  parse_login_response(rej)[1])
        b.close()

        # 3. Duplicate nickname -> rejected.
        dup = Client("Alice", PASSWORD)
        msgs = dup.read_messages(1.0)
        rej = wait_for(msgs, LOGIN_REJECTED)
        check("duplicate nickname rejected", rej is not None,
              f"got types {sorted(set(t for t, _ in msgs))}")
        dup.close()

        # 4. Second valid player joins.
        c = Client("Carol", PASSWORD)
        msgs = c.read_messages(1.0)
        resp = wait_for(msgs, LOGIN_ACCEPTED)
        check("second player accepted", resp is not None)
        carol_id = parse_login_response(resp)[0] if resp else 0
        check("distinct player ids", alice_id != carol_id and alice_id != 0 and carol_id != 0,
              f"alice={alice_id} carol={carol_id}")

        # 5. Chat relay: Alice sends, Carol should receive it.
        a.drain()
        c.drain()
        a.sock.sendall(chat_send("hello from alice"))
        time.sleep(0.3)
        c_msgs = c.read_messages(1.0)
        chat = wait_for(c_msgs, CHAT_MESSAGE)
        check("chat relayed to other player", chat is not None,
              f"got types {sorted(set(t for t, _ in c_msgs))}")
        if chat:
            sid, sender, text = parse_chat(chat)
            check("chat sender is server-authoritative", sender == "Alice" and sid == alice_id,
                  f"sender={sender} id={sid}")
            check("chat text intact", text == "hello from alice", text)

        # 6. Snapshots broadcast to both players, containing both ships.
        a.drain()
        c.drain()
        a.sock.sendall(input_state(1, 1.0, 0.0))
        time.sleep(0.3)
        a_msgs = a.read_messages(1.0)
        c_msgs = c.read_messages(1.0)
        a_snap = last_of(a_msgs, SNAPSHOT)
        c_snap = last_of(c_msgs, SNAPSHOT)
        check("snapshot sent to Alice", a_snap is not None)
        check("snapshot sent to Carol", c_snap is not None)
        if a_snap:
            tick, ships = parse_snapshot(a_snap)
            ids = sorted(s[0] for s in ships)
            check("Alice's snapshot contains both ships", ids == sorted([alice_id, carol_id]),
                  f"ids={ids}")
        if c_snap:
            tick, ships = parse_snapshot(c_snap)
            ids = sorted(s[0] for s in ships)
            check("Carol's snapshot contains both ships", ids == sorted([alice_id, carol_id]),
                  f"ids={ids}")

        # 7. Input relay: Alice thrusts, her ship should move in the snapshot.
        a.drain()
        c.drain()
        a.sock.sendall(input_state(1, 1.0, 0.0))
        time.sleep(0.5)
        c_msgs = c.read_messages(1.0)
        c_snap = last_of(c_msgs, SNAPSHOT)
        if c_snap:
            tick, ships = parse_snapshot(c_snap)
            alice_ship = next((s for s in ships if s[0] == alice_id), None)
            check("Alice's ship moved after thrust",
                  alice_ship is not None and (abs(alice_ship[1]) > 1.0 or abs(alice_ship[2]) > 1.0),
                  f"ship={alice_ship}")

        a.close()
        c.close()
    finally:
        server.terminate()
        try:
            out, _ = server.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            out, _ = server.communicate()
        print("\n--- server log ---")
        print(out)

    if failures:
        print(f"\n{len(failures)} test(s) FAILED: {failures}")
        return 1
    print("\nAll tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
