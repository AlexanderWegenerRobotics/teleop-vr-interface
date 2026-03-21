import socket
import struct
import time
import threading

UNREAL_IP = "127.0.0.1"

AVATAR_RECV_PORT = 7000
AVATAR_SEND_PORT = 8000

STATE_NAMES = {
    0: "OFFLINE",
    1: "IDLE",
    2: "HOMING",
    3: "AWAITING",
    4: "ENGAGED",
    5: "PAUSED",
    6: "FAULT",
    7: "STOP",
}

AVATAR_STATE_FMT = "<BQ"
AVATAR_CMD_FMT = "<BBQ"

class AvatarStub:
    def __init__(self):
        self.state = 1
        self.running = True

        self.recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.recv_sock.bind(("0.0.0.0", AVATAR_RECV_PORT))
        self.recv_sock.settimeout(0.1)

        self.send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        self.recv_thread = threading.Thread(target=self.receive_loop, daemon=True)
        self.heartbeat_thread = threading.Thread(target=self.heartbeat_loop, daemon=True)

    def start(self):
        self.recv_thread.start()
        self.heartbeat_thread.start()
        print(f"Avatar stub running")
        print(f"  Listening for commands on :{AVATAR_RECV_PORT}")
        print(f"  Sending state to {UNREAL_IP}:{AVATAR_SEND_PORT}")
        print(f"  Current state: {STATE_NAMES[self.state]}")
        print()

    def send_state(self):
        ts = int(time.time() * 1e9)
        data = struct.pack(AVATAR_STATE_FMT, self.state, ts)
        self.send_sock.sendto(data, (UNREAL_IP, AVATAR_SEND_PORT))

    def heartbeat_loop(self):
        while self.running:
            self.send_state()
            time.sleep(0.05)

    def receive_loop(self):
        while self.running:
            try:
                data, addr = self.recv_sock.recvfrom(1024)
            except socket.timeout:
                continue

            if len(data) != struct.calcsize(AVATAR_CMD_FMT):
                print(f"  [WARN] unexpected packet size: {len(data)}")
                continue

            requested_state, session_id, ts = struct.unpack(AVATAR_CMD_FMT, data)
            old_state = self.state
            self.handle_command(requested_state)

            if old_state != self.state:
                print(f"  {STATE_NAMES.get(old_state, '?')} -> {STATE_NAMES.get(self.state, '?')}")

    def handle_command(self, requested):
        if requested == 7:
            self.state = 1
            return

        if requested == 1:
            self.state = 1
            return

        if self.state == 1 and requested == 2:
            self.state = 2
            print("  Homing... ", end="", flush=True)
            threading.Thread(target=self.simulate_homing, daemon=True).start()
            return

        if requested == 4 and self.state == 3:
            self.state = 4
            return

        if requested == 5 and self.state == 4:
            self.state = 5
            return

        if requested == 4 and self.state == 5:
            self.state = 4
            return

    def simulate_homing(self):
        time.sleep(2.0)
        if self.state == 2:
            self.state = 3
            print("done. Now AWAITING.")

    def stop(self):
        self.running = False
        self.recv_sock.close()
        self.send_sock.close()


if __name__ == "__main__":
    stub = AvatarStub()
    stub.start()

    print("Press Ctrl+C to quit\n")
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        print("\nStopping...")
        stub.stop()
