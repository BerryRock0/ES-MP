#!/usr/bin/env python3
"""Functional test for the ES-MP LAN server.

Speaks the NetworkProtocol wire format directly so it exercises the real
server binary end to end:
  * login accepted with the correct password
  * login rejected with the wrong password
  * duplicate nickname rejected
  * chat relayed between players
  * snapshots broadcast to every player
  * a disconnected nickname can rejoin
  * server persistence stays outside client storage
"""
import os
from pathlib import Path
import base64
import socket
import struct
import subprocess
import sys
import tempfile
import time

VERSION = 5
MAX_NAME = 32
MAX_PASS = 64
MAX_REASON = 128
MAX_CHAT = 256
MAX_SYSTEM = 32

LOGIN_REQUEST = 1
LOGIN_ACCEPTED = 2
LOGIN_REJECTED = 3
PLAYER_INPUT = 4
SNAPSHOT = 5
CHAT_MESSAGE = 6
CHAT_SEND = 7
DISCONNECT = 8
SERVER_SHUTDOWN = 9
SHIP_STATE = 11
PILOT_SAVE = 13
SAVED_PILOT = 14

# A realistic multi-line pilot save. The server must persist it verbatim and
# hand it back to the same nickname on a later login, so the player's real
# progress (credits, ships, outfits, missions, conditions) survives restarts.
PILOT_TEXT = (
    "pilot Alice Starfarer\n"
    "original name Alice Starfarer\n"
    "date 28 11 3013\n"
    "system Rutilicus\n"
    "planet New Boston\n"
    "credits 1234567\n"
    "ship Shuttle \"Alice's Shuttle\"\n"
    "\tsystem Rutilicus\n"
    "\tplanet New Boston\n"
)

PORT = 47699
PASSWORD = "hunter2"
SERVER_BINARY = Path(os.environ.get(
    "ES_SERVER",
    str(Path(__file__).resolve().parents[1] / "build" / "endless-sky-server"),
))


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


def ship_state(x, y, vx, vy, ang, system):
    sys_bytes = system.encode()[:MAX_SYSTEM - 1].ljust(MAX_SYSTEM, b"\0")
    return frame(SHIP_STATE, struct.pack("<ddddd", x, y, vx, vy, ang) + sys_bytes)


def pilot_save(text):
    data = text.encode("utf-8")
    return frame(PILOT_SAVE, struct.pack("<I", len(data)) + data)


def parse_pilot_payload(payload):
    length = struct.unpack("<I", payload[:4])[0]
    return payload[4:4 + length].decode("utf-8")


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

    def disconnect(self):
        """Send a graceful leave before closing the socket."""
        try:
            self.sock.sendall(frame(DISCONNECT, b""))
        except OSError:
            pass
        self.close()

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
    record = 4 + 40 + 4 + MAX_NAME + MAX_SYSTEM
    for _ in range(count):
        sid = struct.unpack("<I", payload[off:off + 4])[0]
        x, y, vx, vy, ang = struct.unpack("<ddddd", payload[off + 4:off + 44])
        hull = struct.unpack("<i", payload[off + 44:off + 48])[0]
        model = payload[off + 48:off + 48 + MAX_NAME].split(b"\0", 1)[0].decode(errors="replace")
        system = payload[off + 48 + MAX_NAME:off + record].split(b"\0", 1)[0].decode(errors="replace")
        ships.append((sid, x, y, hull, model, system))
        off += record
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


def choose_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def main():
    global PORT
    PORT = choose_port()
    if not SERVER_BINARY.is_file():
        print(f"Server binary not found: {SERVER_BINARY}")
        return 1

    failures = []

    def check(name, cond, detail=""):
        status = "PASS" if cond else "FAIL"
        print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
        if not cond:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="es-mp-lan-") as temporary:
        root = Path(temporary)
        client_directory = root / "client"
        server_directory = root / "server"
        client_directory.mkdir()
        server_directory.mkdir()
        client_sentinel = client_directory / "pilot.txt"
        client_sentinel.write_text("local pilot must remain unchanged\n", encoding="utf-8")

        server = subprocess.Popen(
            [str(SERVER_BINARY), "--port", str(PORT), "--password", PASSWORD,
             "--name", "TestHost", "--save-dir", str(server_directory)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(0.4)
        a = b = dup = c = rejoined = None

        try:
            # 1. Correct password -> accepted.
            a = Client("Alice", PASSWORD)
            msgs = a.read_messages(1.0)
            resp = wait_for(msgs, LOGIN_ACCEPTED)
            check("login accepted with correct password", resp is not None,
                  f"got types {sorted(set(t for t, _ in msgs))}")
            alice_id = parse_login_response(resp)[0] if resp else 0
            check("no stored pilot save for a first-time player", wait_for(msgs, SAVED_PILOT) is None,
                  f"got types {sorted(set(t for t, _ in msgs))}")

            # 1.1. Alice uploads her full pilot save. The server must keep it
            # so her real progress survives a disconnect and a server restart.
            a.sock.sendall(pilot_save(PILOT_TEXT))
            time.sleep(0.2)
            a.drain()

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
            b = None

            # 3. Control characters in a nickname cannot inject world-file lines.
            injected = Client("Alice\nsystem Owned", PASSWORD)
            msgs = injected.read_messages(1.0)
            check("control characters in nickname rejected",
                  wait_for(msgs, LOGIN_REJECTED) is not None,
                  f"got types {sorted(set(t for t, _ in msgs))}")
            injected.close()

            # 4. Duplicate nickname -> rejected while the first connection is live.
            dup = Client("Alice", PASSWORD)
            msgs = dup.read_messages(1.0)
            rej = wait_for(msgs, LOGIN_REJECTED)
            check("duplicate nickname rejected", rej is not None,
                  f"got types {sorted(set(t for t, _ in msgs))}")
            dup.close()
            dup = None

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

            # 7. State relay: Alice reports her ship's absolute position and
            #    system; Carol's snapshot must reflect it.
            a.drain()
            c.drain()
            a.sock.sendall(ship_state(1234.5, -678.25, 10.0, 0.0, 45.0, "TestSystem"))
            time.sleep(0.3)
            c_msgs = c.read_messages(1.0)
            c_snap = last_of(c_msgs, SNAPSHOT)
            if c_snap:
                tick, ships = parse_snapshot(c_snap)
                alice_ship = next((s for s in ships if s[0] == alice_id), None)
                check("Alice's position relayed to Carol",
                      alice_ship is not None and abs(alice_ship[1] - 1234.5) < 0.001
                      and abs(alice_ship[2] + 678.25) < 0.001,
                      f"ship={alice_ship}")
                check("Alice's system relayed to Carol",
                      alice_ship is not None and alice_ship[5] == "TestSystem",
                      f"ship={alice_ship}")

            # 8. A graceful leave frees the nickname for a second entry, and
            #    the stored pilot save is handed back so progress is kept.
            a.disconnect()
            a = None
            time.sleep(0.3)
            rejoined = Client("Alice", PASSWORD)
            msgs = rejoined.read_messages(1.0)
            check("same nickname can rejoin after disconnect",
                  wait_for(msgs, LOGIN_ACCEPTED) is not None,
                  f"got types {sorted(set(t for t, _ in msgs))}")
            saved = wait_for(msgs, SAVED_PILOT)
            check("stored pilot save returned on rejoin", saved is not None,
                  f"got types {sorted(set(t for t, _ in msgs))}")
            if saved:
                check("stored pilot save text intact on rejoin",
                      parse_pilot_payload(saved) == PILOT_TEXT,
                      repr(parse_pilot_payload(saved)[:60]))
        finally:
            for client in (rejoined, a, c, dup, b):
                if client is not None:
                    client.close()
            if server.poll() is None:
                server.terminate()
            try:
                out, _ = server.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                out, _ = server.communicate()
            print("\n--- server log ---")
            print(out)

        world_file = server_directory / "world.txt"
        check("server persists its world in its own directory", world_file.is_file(),
              str(world_file))
        check("client storage is not used for the server world",
              not (client_directory / "world.txt").exists())
        check("client save remains unchanged",
              client_sentinel.read_text(encoding="utf-8") == "local pilot must remain unchanged\n")

        # The world file must keep each player's full pilot save (base64 on
        # its own line), not just a position, so a restart restores progress.
        stored_pilot_line = ""
        if world_file.is_file():
            for line in world_file.read_text(encoding="utf-8").splitlines():
                stripped = line.strip()
                if stripped.startswith("pilot "):
                    stored_pilot_line = stripped[len("pilot "):]
                    break
            check("world file records the pilot save", len(stored_pilot_line) > 0,
                  stored_pilot_line[:40] if stored_pilot_line else "no pilot line")
            if stored_pilot_line:
                try:
                    decoded = base64.b64decode(stored_pilot_line, validate=True).decode("utf-8")
                    check("pilot save round-trips through the world file",
                          decoded == PILOT_TEXT, repr(decoded[:60]))
                except Exception:
                    check("pilot save round-trips through the world file", False)

        # 9. Restart the server on the same save directory: a returning player
        #    must get their stored pilot save back, proving real progress (not
        #    just positions) survives a server restart.
        server2 = subprocess.Popen(
            [str(SERVER_BINARY), "--port", str(PORT), "--password", PASSWORD,
             "--name", "TestHost", "--save-dir", str(server_directory)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            time.sleep(0.4)
            again = Client("Alice", PASSWORD)
            msgs = again.read_messages(1.5)
            check("rejoin accepted after server restart",
                  wait_for(msgs, LOGIN_ACCEPTED) is not None,
                  f"got types {sorted(set(t for t, _ in msgs))}")
            saved = wait_for(msgs, SAVED_PILOT)
            check("stored pilot save returned after server restart",
                  saved is not None, f"got types {sorted(set(t for t, _ in msgs))}")
            if saved:
                check("stored pilot save survives a server restart",
                      parse_pilot_payload(saved) == PILOT_TEXT,
                      repr(parse_pilot_payload(saved)[:60]))
            again.close()
        finally:
            if server2.poll() is None:
                server2.terminate()
            try:
                out2, _ = server2.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                server2.kill()
                out2, _ = server2.communicate()
            out2 = None

    if failures:
        print(f"\n{len(failures)} test(s) FAILED: {failures}")
        return 1
    print("\nAll tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
