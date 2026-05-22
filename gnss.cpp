[22-05-2026 10:21] Yash Zanwar: import json
import math
import os
import threading
import time
from datetime import datetime, timezone

from flask import Flask, jsonify, request, send_from_directory
import serial

app = Flask(name)

TRANSMITTER_PORT = os.getenv("TRANSMITTER_PORT", "COM8")
RECEIVER_PORT = os.getenv("RECEIVER_PORT", "COM5")

TRANSMITTER_BAUD = 115200
RECEIVER_BAUD = 9600
MIRROR_RECEIVER = os.getenv("MIRROR_RECEIVER", "1").lower() in (
    "1",
    "true",
    "yes",
    "on",
)
FORCE_RECEIVER_FROM_TRANSMITTER = os.getenv(
    "FORCE_RECEIVER_FROM_TRANSMITTER", "1"
).lower() in ("1", "true", "yes", "on")

latest_transmitter = None
latest_receiver = None
latest_uav_status = None
latest_joystick = None
receiver_log = []
joystick_log = []
security_metrics = None
security_history = []
security_events = []
security_engine = {
    "prev_lat": None,
    "prev_lon": None,
    "prev_ts": None,
    "jitter_points": [],
    "signal_ema": 0.0,
    "fusion_ema": 0.0,
    "jump_ema": 0.0,
    "risk_ema": 0.0,
    "state": "CLEAN",
}
security_config = {
    "alpha": 0.30,
    "max_step": 7.0,
    "warning_on": 45.0,
    "warning_off": 35.0,
    "alert_on": 70.0,
    "alert_off": 58.0,
    "weights": {"signal": 0.38, "fusion": 0.34, "jump": 0.28},
}
data_lock = threading.Lock()
serial_lock = threading.Lock()
serial_handles = {"transmitter": None, "receiver": None}


def parse_value(raw):
    raw = raw.strip()
    if raw == "":
        return None
    try:
        if "." in raw or "e" in raw.lower():
            return float(raw)
        return int(raw)
    except ValueError:
        return raw


def parse_packet(line):
    parts = [p.strip() for p in line.split(",") if p.strip()]
    if not parts:
        return None
    data = {}
    for part in parts:
        if ":" not in part:
            continue
        key, value = part.split(":", 1)
        key = key.strip().upper()
        data[key] = parse_value(value)
    return data if data else None


def normalize_packet(data, source):
    packet = dict(data)
    packet["source"] = source
    packet["timestamp"] = datetime.now(timezone.utc).isoformat()
    if source == "receiver":
        packet["RSSI"] = packet.get("RSSI", None)
        packet["SNR"] = packet.get("SNR", None)
    else:
        packet["RSSI"] = None
        packet["SNR"] = None
    return packet


def clamp(value, min_value, max_value):
    return max(min_value, min(max_value, value))


def safe_float(value):
    try:
        if value is None:
            return None
        return float(value)
    except (TypeError, ValueError):
        return None


def haversine_m(lat1, lon1, lat2, lon2):
    radius = 6371000.0
    d_lat = math.radians(lat2 - lat1)
    d_lon = math.radians(lon2 - lon1)
    a = (
        math.sin(d_lat / 2) ** 2
        + math.cos(math.radians(lat1))
        * math.cos(math.radians(lat2))
        * math.sin(d_lon / 2) ** 2
    )
    return 2 * radius * math.atan2(math.sqrt(a), math.sqrt(1 - a))


def _set_security_state_locked(new_state):
    global security_events
    old_state = security_engine["state"]
    if old_state == new_state:
        return
    security_engine["state"] = new_state
    security_events.append(
        {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "from": old_state,
            "to": new_state,
            "message": f"State changed from {old_state} to {new_state}",
        }
    )
    if len(security_events) > 60:
        security_events = security_events[-60:]


def recompute_security_metrics_locked():
    global security_metrics, security_history
    rx = latest_receiver or latest_transmitter
    tx = latest_transmitter
    if rx is None:
        return

    lat = safe_float(rx.get("LAT"))
    lon = safe_float(rx.get("LON"))
    gfix = safe_float(rx.get("GFIX"))
    ax = safe_float(rx.get("AX")) or 0.0
[22-05-2026 10:21] Yash Zanwar: ay = safe_float(rx.get("AY")) or 0.0
    az = safe_float(rx.get("AZ")) or 9.81
    tilt = int(safe_float(rx.get("TILT")) or 0)
    pir = int(safe_float(rx.get("PIR")) or 0)
    ts_raw = rx.get("timestamp")
    ts = datetime.now(timezone.utc)
    if isinstance(ts_raw, str):
        try:
            ts = datetime.fromisoformat(ts_raw)
            if ts.tzinfo is None:
                ts = ts.replace(tzinfo=timezone.utc)
        except ValueError:
            pass

    jump = 0.0
    speed = 0.0
    prev_lat = security_engine["prev_lat"]
    prev_lon = security_engine["prev_lon"]
    prev_ts = security_engine["prev_ts"]
    if (
        lat is not None
        and lon is not None
        and prev_lat is not None
        and prev_lon is not None
        and prev_ts is not None
    ):
        jump = haversine_m(prev_lat, prev_lon, lat, lon)
        dt = max(0.2, (ts - prev_ts).total_seconds())
        speed = jump / dt

    if lat is not None and lon is not None:
        security_engine["prev_lat"] = lat
        security_engine["prev_lon"] = lon
        security_engine["prev_ts"] = ts

        security_engine["jitter_points"].append((lat, lon))
        if len(security_engine["jitter_points"]) > 12:
            security_engine["jitter_points"] = security_engine["jitter_points"][-12:]

    jitter_score = 0.0
    pts = security_engine["jitter_points"]
    if len(pts) >= 5:
        lat_mean = sum(p[0] for p in pts) / len(pts)
        lon_mean = sum(p[1] for p in pts) / len(pts)
        lat_std = math.sqrt(sum((p[0] - lat_mean) ** 2 for p in pts) / len(pts))
        lon_std = math.sqrt(sum((p[1] - lon_mean) ** 2 for p in pts) / len(pts))
        jitter_score = clamp(max(lat_std, lon_std) * 900000, 0.0, 30.0)

    mismatch_score = 0.0
    mismatch_deg = 0.0
    if tx is not None and lat is not None and lon is not None:
        tx_lat = safe_float(tx.get("LAT"))
        tx_lon = safe_float(tx.get("LON"))
        if tx_lat is not None and tx_lon is not None:
            mismatch_deg = max(abs(lat - tx_lat), abs(lon - tx_lon))
            mismatch_score = clamp((mismatch_deg - 0.0003) * 60000, 0.0, 70.0)

    fix_score = 10.0 if gfix is None else (28.0 if gfix <= 0 else 0.0)
    signal_raw = clamp(fix_score + jitter_score + mismatch_score, 0.0, 100.0)

    accel_mag = math.sqrt(ax * ax + ay * ay + az * az)
    dyn_accel = abs(accel_mag - 9.81)
    fusion_raw = 0.0
    if speed > 6.0 and dyn_accel < 0.15:
        fusion_raw += 45.0
    if speed < 0.6 and dyn_accel > 2.4:
        fusion_raw += 30.0
    if tilt == 1 and speed > 8.0:
        fusion_raw += 15.0
    if pir == 1 and speed > 10.0:
        fusion_raw += 10.0
    fusion_raw = clamp(fusion_raw, 0.0, 100.0)

    if jump > 45:
        jump_raw = 100.0
    elif jump > 25:
        jump_raw = 75.0
    elif jump > 12:
        jump_raw = 50.0
    elif jump > 6:
        jump_raw = 24.0
    else:
        jump_raw = 0.0

    alpha = float(security_config["alpha"])
    max_step = float(security_config["max_step"])
    weights = security_config["weights"]
    security_engine["signal_ema"] += alpha * (signal_raw - security_engine["signal_ema"])
    security_engine["fusion_ema"] += alpha * (fusion_raw - security_engine["fusion_ema"])
    security_engine["jump_ema"] += alpha * (jump_raw - security_engine["jump_ema"])

    target_risk = (
        security_engine["signal_ema"] * float(weights["signal"])
        + security_engine["fusion_ema"] * float(weights["fusion"])
        + security_engine["jump_ema"] * float(weights["jump"])
    )
    delta = target_risk - security_engine["risk_ema"]
    if abs(delta) > max_step:
        target_risk = security_engine["risk_ema"] + math.copysign(max_step, delta)
    security_engine["risk_ema"] = clamp(target_risk, 0.0, 100.0)

    risk = security_engine["risk_ema"]
[22-05-2026 10:21] Yash Zanwar: state = security_engine["state"]
    warning_on = float(security_config["warning_on"])
    warning_off = float(security_config["warning_off"])
    alert_on = float(security_config["alert_on"])
    alert_off = float(security_config["alert_off"])
    if state == "CLEAN":
        if risk >= alert_on:
            _set_security_state_locked("SPOOFING SUSPECTED")
        elif risk >= warning_on:
            _set_security_state_locked("ELEVATED RISK")
    elif state == "ELEVATED RISK":
        if risk >= alert_on:
            _set_security_state_locked("SPOOFING SUSPECTED")
        elif risk <= warning_off:
            _set_security_state_locked("CLEAN")
    else:
        if risk <= alert_off:
            if risk <= warning_off:
                _set_security_state_locked("CLEAN")
            else:
                _set_security_state_locked("ELEVATED RISK")

    security_metrics = {
        "timestamp": ts.isoformat(),
        "state": security_engine["state"],
        "risk": round(risk, 2),
        "signal_score": round(security_engine["signal_ema"], 2),
        "fusion_score": round(security_engine["fusion_ema"], 2),
        "jump_score": round(security_engine["jump_ema"], 2),
        "raw": {
            "signal": round(signal_raw, 2),
            "fusion": round(fusion_raw, 2),
            "jump": round(jump_raw, 2),
        },
        "diagnostics": {
            "jump_m": round(jump, 2),
            "speed_mps": round(speed, 2),
            "dyn_accel": round(dyn_accel, 3),
            "mismatch_deg": round(mismatch_deg, 6),
            "gfix": gfix,
        },
        "algorithm": {
            "mode": "backend-ema-hysteresis",
            "alpha": alpha,
            "max_step": max_step,
            "warning_on": warning_on,
            "warning_off": warning_off,
            "alert_on": alert_on,
            "alert_off": alert_off,
        },
    }
    security_history.append(round(risk, 2))
    if len(security_history) > 180:
        security_history = security_history[-180:]


def mirror_packet(packet):
    mirrored = dict(packet)
    mirrored["source"] = "receiver"
    mirrored["mirrored"] = True
    mirrored["RSSI"] = None
    mirrored["SNR"] = None
    return mirrored


def update_latest(packet, source):
    global latest_transmitter, latest_receiver, receiver_log
    with data_lock:
        if source == "transmitter":
            latest_transmitter = packet
            if FORCE_RECEIVER_FROM_TRANSMITTER:
                mirrored = mirror_packet(packet)
                latest_receiver = mirrored
                receiver_log.append(mirrored)
                if len(receiver_log) > 20:
                    receiver_log = receiver_log[-20:]
            elif MIRROR_RECEIVER and (
                latest_receiver is None or latest_receiver.get("mirrored")
            ):
                mirrored = mirror_packet(packet)
                latest_receiver = mirrored
                receiver_log.append(mirrored)
                if len(receiver_log) > 20:
                    receiver_log = receiver_log[-20:]
        else:
            if FORCE_RECEIVER_FROM_TRANSMITTER:
                return
            packet["mirrored"] = False
            latest_receiver = packet
            receiver_log.append(packet)
            if len(receiver_log) > 20:
                receiver_log = receiver_log[-20:]
        recompute_security_metrics_locked()


def serial_reader(port, baud, source):
    global latest_uav_status, latest_joystick, joystick_log
    while True:
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                with serial_lock:
                    serial_handles[source] = ser
                print(f"[{source}] Connected on {port} @ {baud}")
                while True:
                    raw = ser.readline()
[22-05-2026 10:21] Yash Zanwar: if not raw:
                        continue
                    line = raw.decode(errors="ignore").strip()
                    if not line:
                        continue
                    if line.startswith("UAV|"):
                        data = parse_packet(line.replace("UAV|", "", 1))
                        if data:
                            packet = normalize_packet(data, "receiver")
                            packet["type"] = "uav_status"
                            with data_lock:
                                latest_uav_status = packet
                        continue
                    if line.startswith("JOY|"):
                        data = parse_packet(line.replace("JOY|", "", 1))
                        if data:
                            event = {
                                "DIR": str(data.get("DIR", "")).upper(),
                                "source": source,
                                "timestamp": datetime.now(timezone.utc).isoformat(),
                                "raw": line,
                            }
                            with data_lock:
                                latest_joystick = event
                                joystick_log.append(event)
                                if len(joystick_log) > 30:
                                    joystick_log = joystick_log[-30:]
                        continue
                    data = parse_packet(line)
                    if not data:
                        continue
                    packet = normalize_packet(data, source)
                    update_latest(packet, source)
        except serial.SerialException as exc:
            with serial_lock:
                serial_handles[source] = None
            print(f"[{source}] Port error on {port}: {exc}")
            print(f"[{source}] Retrying in 5 seconds...")
            time.sleep(5)


def send_command(target, payload):
    with serial_lock:
        ser = serial_handles.get(target)
        if ser is None or not ser.is_open:
            return False, f"{target} serial not connected"
        try:
            ser.write(payload.encode("utf-8") + b"\n")
            ser.flush()
            return True, "sent"
        except serial.SerialException as exc:
            return False, f"serial write failed: {exc}"


@app.route("/")
def index():
    return send_from_directory(os.path.dirname(file), "index.html")


@app.route("/geofence")
def geofence():
    return send_from_directory(os.path.dirname(file), "index.html")


@app.route("/motion")
def motion():
    return send_from_directory(os.path.dirname(file), "motion.html")


@app.route("/gnss-denied")
def gnss_denied():
    return send_from_directory(os.path.dirname(file), "gnss_denied.html")


@app.route("/security-radar")
def security_radar():
    return send_from_directory(os.path.dirname(file), "security_radar.html")


@app.route("/transmitter")
def transmitter():
    with data_lock:
        return jsonify(latest_transmitter or {"status": "no data"})


@app.route("/receiver")
def receiver():
    with data_lock:
        return jsonify(latest_receiver or {"status": "no data"})


@app.route("/both")
def both():
    with data_lock:
        return jsonify(
            {
                "transmitter": latest_transmitter,
                "receiver": latest_receiver,
                "receiver_log": receiver_log,
                "uav_status": latest_uav_status,
                "joystick": latest_joystick,
                "joystick_log": joystick_log,
                "security_metrics": security_metrics,
            }
        )


@app.route("/joystick")
def joystick():
    with data_lock:
        return jsonify({"latest": latest_joystick, "log": joystick_log})


@app.route("/security-metrics")
[22-05-2026 10:21] Yash Zanwar: def get_security_metrics():
    with data_lock:
        return jsonify(
            {
                "metrics": security_metrics,
                "history": security_history[-120:],
                "events": security_events[-12:],
                "config": security_config,
            }
        )


@app.route("/security-config", methods=["GET", "POST"])
def security_config_route():
    with data_lock:
        if request.method == "POST":
            data = request.get_json(silent=True) or {}
            alpha = safe_float(data.get("alpha"))
            max_step = safe_float(data.get("max_step"))
            warning_on = safe_float(data.get("warning_on"))
            alert_on = safe_float(data.get("alert_on"))

            if alpha is not None:
                security_config["alpha"] = clamp(alpha, 0.05, 0.95)
            if max_step is not None:
                security_config["max_step"] = clamp(max_step, 1.0, 25.0)
            if warning_on is not None:
                security_config["warning_on"] = clamp(warning_on, 20.0, 80.0)
            if alert_on is not None:
                security_config["alert_on"] = clamp(alert_on, 35.0, 95.0)

            if security_config["alert_on"] <= security_config["warning_on"] + 5:
                security_config["alert_on"] = security_config["warning_on"] + 5
            security_config["warning_off"] = max(10.0, security_config["warning_on"] - 10)
            security_config["alert_off"] = max(
                security_config["warning_on"] + 1, security_config["alert_on"] - 12
            )

            recompute_security_metrics_locked()
            return jsonify({"ok": True, "config": security_config})

        return jsonify({"config": security_config})


@app.route("/command", methods=["POST"])
def command():
    data = request.get_json(silent=True) or {}
    target = data.get("target", "receiver")
    if target not in ("receiver", "transmitter"):
        return jsonify({"ok": False, "error": "invalid target"}), 400

    command_payload = data.get("command")
    if command_payload is None:
        return jsonify({"ok": False, "error": "missing command"}), 400

    if isinstance(command_payload, dict):
        line = json.dumps(command_payload)
    else:
        line = str(command_payload)

    ok, message = send_command(target, line)
    status = 200 if ok else 503
    return jsonify({"ok": ok, "message": message, "target": target}), status


if name == "main":
    threading.Thread(
        target=serial_reader,
        args=(TRANSMITTER_PORT, TRANSMITTER_BAUD, "transmitter"),
        daemon=True,
    ).start()
    threading.Thread(
        target=serial_reader,
        args=(RECEIVER_PORT, RECEIVER_BAUD, "receiver"),
        daemon=True,
    ).start()

    app.run(host="0.0.0.0", port=5000, debug=False, use_reloader=False)