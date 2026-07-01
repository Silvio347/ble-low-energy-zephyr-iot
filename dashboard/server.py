#!/usr/bin/env python3
import json
import os
import queue
import socket
import ssl
import struct
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parent
CONFIG_FILE = ROOT / "config.local.json"
KEEPALIVE_SECONDS = 30
PORT_ATTEMPTS = 20


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def load_config():
    defaults = {
        "mqtt_host": "03e613ef35b443dfb6cb61216465f006.s1.eu.hivemq.cloud",
        "mqtt_port": 8883,
        "mqtt_username": "hivemq-user",
        "mqtt_password": "Hivemq-password123",
        "mqtt_client_id": "local-dashboard-ble",
        "mqtt_base_topic": "lowble",
        "mqtt_tls": True,
        "http_host": "127.0.0.1",
        "http_port": 8080,
    }
    if CONFIG_FILE.exists():
        with CONFIG_FILE.open("r", encoding="utf-8") as fh:
            defaults.update(json.load(fh))

    env_map = {
        "MQTT_HOST": "mqtt_host",
        "MQTT_PORT": "mqtt_port",
        "MQTT_USERNAME": "mqtt_username",
        "MQTT_PASSWORD": "mqtt_password",
        "MQTT_CLIENT_ID": "mqtt_client_id",
        "MQTT_BASE_TOPIC": "mqtt_base_topic",
        "HTTP_HOST": "http_host",
        "HTTP_PORT": "http_port",
    }
    for env_name, key in env_map.items():
        if os.environ.get(env_name):
            defaults[key] = os.environ[env_name]

    defaults["mqtt_port"] = int(defaults["mqtt_port"])
    defaults["http_port"] = int(defaults["http_port"])
    defaults["mqtt_tls"] = str(defaults.get("mqtt_tls", True)).lower() not in {"0", "false", "no"}
    return defaults


class EventBus:
    def __init__(self):
        self.lock = threading.Lock()
        self.clients = []
        self.history = deque(maxlen=250)
        self.latest = None
        self.status = {
            "connected": False,
            "message": "Inicializando",
            "last_error": None,
            "updated_at": now_iso(),
        }

    def subscribe(self):
        client_queue = queue.Queue(maxsize=100)
        with self.lock:
            self.clients.append(client_queue)
            client_queue.put(("status", self.status))
            if self.latest:
                client_queue.put(("message", self.latest))
        return client_queue

    def unsubscribe(self, client_queue):
        with self.lock:
            if client_queue in self.clients:
                self.clients.remove(client_queue)

    def publish_status(self, **kwargs):
        status = {**self.status, **kwargs, "updated_at": now_iso()}
        with self.lock:
            self.status = status
        self._fanout("status", status)

    def publish_message(self, message):
        with self.lock:
            self.latest = message
            self.history.appendleft(message)
        self._fanout("message", message)

    def snapshot(self):
        with self.lock:
            return {
                "status": self.status,
                "latest": self.latest,
                "history": list(self.history)[:100],
            }

    def _fanout(self, event_name, payload):
        with self.lock:
            clients = list(self.clients)
        for client_queue in clients:
            try:
                client_queue.put_nowait((event_name, payload))
            except queue.Full:
                pass


def mqtt_string(value):
    data = str(value).encode("utf-8")
    return struct.pack("!H", len(data)) + data


def encode_remaining_length(length):
    encoded = bytearray()
    while True:
        digit = length % 128
        length //= 128
        if length > 0:
            digit |= 0x80
        encoded.append(digit)
        if length == 0:
            return bytes(encoded)


def mqtt_packet(packet_type, payload):
    return bytes([packet_type]) + encode_remaining_length(len(payload)) + payload


def read_exact(sock, size):
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("Conexao encerrada pelo broker")
        chunks.extend(chunk)
    return bytes(chunks)


def read_packet(sock):
    first = read_exact(sock, 1)[0]
    multiplier = 1
    remaining = 0
    while True:
        encoded_byte = read_exact(sock, 1)[0]
        remaining += (encoded_byte & 127) * multiplier
        if (encoded_byte & 128) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise ValueError("Tamanho MQTT invalido")
    return first, read_exact(sock, remaining)


class MqttBridge(threading.Thread):
    def __init__(self, config, bus):
        super().__init__(daemon=True)
        self.config = config
        self.bus = bus
        self.packet_id = 1
        self.stop_event = threading.Event()

    def run(self):
        while not self.stop_event.is_set():
            try:
                self.connect_and_loop()
            except Exception as exc:
                self.bus.publish_status(
                    connected=False,
                    message="Reconectando",
                    last_error=str(exc),
                    host=self.config["mqtt_host"],
                    topic=f'{self.config["mqtt_base_topic"]}/#',
                )
                time.sleep(3)

    def connect_and_loop(self):
        host = self.config["mqtt_host"]
        port = self.config["mqtt_port"]
        base_topic = self.config["mqtt_base_topic"].rstrip("/")
        topic_filters = [base_topic, f"{base_topic}/#"]
        topic_label = ", ".join(topic_filters)

        self.bus.publish_status(
            connected=False,
            message="Conectando ao MQTT",
            last_error=None,
            host=host,
            topic=topic_label,
        )

        raw_sock = socket.create_connection((host, port), timeout=10)
        if self.config["mqtt_tls"]:
            context = ssl.create_default_context()
            sock = context.wrap_socket(raw_sock, server_hostname=host)
        else:
            sock = raw_sock
        sock.settimeout(1)

        with sock:
            self._connect(sock)
            self._subscribe(sock, topic_filters)
            self.bus.publish_status(
                connected=True,
                message="Recebendo dados",
                last_error=None,
                host=host,
                topic=topic_label,
            )
            last_ping = time.monotonic()
            while not self.stop_event.is_set():
                if time.monotonic() - last_ping > KEEPALIVE_SECONDS / 2:
                    sock.sendall(mqtt_packet(0xC0, b""))
                    last_ping = time.monotonic()
                try:
                    first, payload = read_packet(sock)
                except socket.timeout:
                    continue
                packet_type = first >> 4
                if packet_type == 3:
                    self._handle_publish(sock, first, payload)
                elif packet_type == 13:
                    continue
                elif packet_type == 9:
                    continue
                elif packet_type == 2:
                    continue

    def _connect(self, sock):
        client_id = f'{self.config["mqtt_client_id"]}-{os.getpid()}'
        variable_header = mqtt_string("MQTT") + bytes([4, 0xC2]) + struct.pack("!H", KEEPALIVE_SECONDS)
        payload = (
            mqtt_string(client_id)
            + mqtt_string(self.config["mqtt_username"])
            + mqtt_string(self.config["mqtt_password"])
        )
        sock.sendall(mqtt_packet(0x10, variable_header + payload))
        packet_type, response = read_packet(sock)
        if packet_type >> 4 != 2 or len(response) < 2:
            raise ConnectionError("Resposta CONNACK invalida")
        if response[1] != 0:
            codes = {
                1: "protocolo recusado",
                2: "client id recusado",
                3: "servidor indisponivel",
                4: "usuario ou senha invalidos",
                5: "nao autorizado",
            }
            raise ConnectionError(f"MQTT recusado: {codes.get(response[1], response[1])}")

    def _subscribe(self, sock, topic_filters):
        packet_id = self._next_packet_id()
        payload = struct.pack("!H", packet_id)
        for topic_filter in topic_filters:
            payload += mqtt_string(topic_filter) + bytes([0])
        sock.sendall(mqtt_packet(0x82, payload))

    def _handle_publish(self, sock, first, payload):
        topic_len = struct.unpack("!H", payload[:2])[0]
        topic = payload[2 : 2 + topic_len].decode("utf-8", errors="replace")
        offset = 2 + topic_len
        qos = (first & 0x06) >> 1
        packet_id = None
        if qos:
            packet_id = struct.unpack("!H", payload[offset : offset + 2])[0]
            offset += 2
        body = payload[offset:].decode("utf-8", errors="replace")
        try:
            parsed = json.loads(body)
        except json.JSONDecodeError:
            parsed = {"raw": body}
        self.bus.publish_message(
            {
                "topic": topic,
                "payload": parsed,
                "raw": body,
                "received_at": now_iso(),
            }
        )
        if qos == 1 and packet_id is not None:
            sock.sendall(mqtt_packet(0x40, struct.pack("!H", packet_id)))

    def _next_packet_id(self):
        self.packet_id += 1
        if self.packet_id > 65535:
            self.packet_id = 1
        return self.packet_id


class DashboardHandler(BaseHTTPRequestHandler):
    bus = None
    config = None

    def log_message(self, fmt, *args):
        print(f"[http] {self.address_string()} - {fmt % args}")

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/events":
            self.handle_events()
            return
        if parsed.path == "/api/state":
            self.send_json(self.bus.snapshot())
            return
        if parsed.path == "/api/config":
            self.send_json(
                {
                    "mqtt_host": self.config["mqtt_host"],
                    "mqtt_base_topic": self.config["mqtt_base_topic"],
                    "mqtt_tls": self.config["mqtt_tls"],
                }
            )
            return
        self.serve_static(parsed.path)

    def do_HEAD(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/state":
            self.send_json(self.bus.snapshot(), head_only=True)
            return
        if parsed.path == "/api/config":
            self.send_json(
                {
                    "mqtt_host": self.config["mqtt_host"],
                    "mqtt_base_topic": self.config["mqtt_base_topic"],
                    "mqtt_tls": self.config["mqtt_tls"],
                },
                head_only=True,
            )
            return
        self.serve_static(parsed.path, head_only=True)

    def send_json(self, payload, head_only=False):
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        if head_only:
            return
        self.wfile.write(encoded)

    def handle_events(self):
        client_queue = self.bus.subscribe()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        try:
            while True:
                try:
                    event_name, payload = client_queue.get(timeout=15)
                    self.wfile.write(f"event: {event_name}\n".encode("utf-8"))
                    self.wfile.write(f"data: {json.dumps(payload)}\n\n".encode("utf-8"))
                except queue.Empty:
                    self.wfile.write(b": keepalive\n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            self.bus.unsubscribe(client_queue)

    def serve_static(self, url_path, head_only=False):
        clean_path = "index.html" if url_path in {"/", ""} else url_path.lstrip("/")
        target = (ROOT / clean_path).resolve()
        if ROOT not in target.parents and target != ROOT:
            self.send_error(HTTPStatus.FORBIDDEN)
            return
        if not target.exists() or target.is_dir():
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        content_types = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
            ".json": "application/json; charset=utf-8",
            ".svg": "image/svg+xml",
        }
        data = target.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_types.get(target.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if head_only:
            return
        self.wfile.write(data)


class DashboardServer(ThreadingHTTPServer):
    allow_reuse_address = True


def create_http_server(config):
    host = config["http_host"]
    first_port = config["http_port"]
    last_error = None
    for port in range(first_port, first_port + PORT_ATTEMPTS):
        try:
            server = DashboardServer((host, port), DashboardHandler)
            if port != first_port:
                print(f"Porta {first_port} ocupada; usando {port}.")
            return server
        except OSError as exc:
            last_error = exc
            if exc.errno != 98:
                raise
    raise OSError(f"Nenhuma porta livre entre {first_port} e {first_port + PORT_ATTEMPTS - 1}") from last_error


def main():
    config = load_config()
    bus = EventBus()

    DashboardHandler.bus = bus
    DashboardHandler.config = config
    server = create_http_server(config)
    config["http_port"] = server.server_address[1]

    bridge = MqttBridge(config, bus)
    bridge.start()

    url = f'http://{config["http_host"]}:{config["http_port"]}'
    print(f"Dashboard local: {url}")
    base_topic = config["mqtt_base_topic"].rstrip("/")
    print(f'Assinando MQTT: {base_topic}, {base_topic}/# em {config["mqtt_host"]}:{config["mqtt_port"]}')
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nEncerrando...")
    finally:
        bridge.stop_event.set()
        server.server_close()


if __name__ == "__main__":
    main()
