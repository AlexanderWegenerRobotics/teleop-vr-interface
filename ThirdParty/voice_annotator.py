"""
Usage:
    python voice_annotator.py [--host 127.0.0.1] [--port 7778] [--device cuda]
    python voice_annotator.py --list-devices
    python voice_annotator.py --device-index 2
"""

import argparse
import json
import queue
import socket
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass
from enum import IntEnum

import numpy as np
import pyaudio
import torch
from faster_whisper import WhisperModel
from silero_vad import load_silero_vad

# ── Audio / VAD constants ────────────────────────────────────────────────────

SAMPLE_RATE      = 16000
CHUNK_SAMPLES    = 512          # silero-vad requires 512 samples @ 16kHz
CHANNELS         = 1
CHUNK_SEC        = CHUNK_SAMPLES / SAMPLE_RATE   # ~32ms per chunk

VAD_THRESHOLD    = 0.4          # lowered from 0.5 — short plosive-initial words trigger more reliably
SILENCE_CHUNKS   = 15           # ~480ms of silence flushes a segment (was 40 → too long for back-to-back commands)
PRE_ROLL_CHUNKS  = 10           # ~320ms pre-roll prepended to each segment to preserve word onset
POST_ROLL_CHUNKS = 6            # ~190ms post-roll trailing each segment
MIN_RECORD_SEC   = 0.3          # discard segments shorter than this
MAX_RECORD_SEC   = 6             # hard cap on a single voice segment

# ── Whisper constants ────────────────────────────────────────────────────────

WHISPER_MODEL       = "small.en"   # 3090 has the VRAM — base.en is too weak for noisy VR mic audio, small, medium
WHISPER_LANGUAGE    = "en"
NO_SPEECH_THRESHOLD = 0.6
LOGPROB_REJECT      = -1.0         # reject transcriptions with avg_logprob below this

# Bias Whisper's language model toward our closed vocabulary — substantial accuracy gain on noisy short speech
INITIAL_PROMPT = (
    "Commands: grasp, insert, handover, handover left, handover right, "
    "place, release, start, engage, stop, reset, pause, "
    "reset left, reset right, home, success, partial, failure, "
    "statistics, viewpoint, camera left, camera right, camera off. "
    "Quality ratings from zero to ten."
)

# ── Vocabulary ────────────────────────────────────────────────────────────────

INTENT_LABELS  = {"grasp", "insert", "handover", "handover-left", "handover-right", "place", "release"}
COMMANDS       = {
    "start", "stop", "reset", "pause", "engage",
    # Per-arm resets (multi-word, normalised below before matching)
    "reset-left", "reset-right",
    # Episode annotation
    "home", "success", "partial", "failure",
    # UI toggles
    "statistics", "viewpoint", "camera-off", "camera-left", "camera-right",
}
QUALITY_PREFIX = "quality"

# Spoken-form corrections: map Whisper mishearings to canonical tokens.
# Multi-word commands are collapsed to hyphenated forms so the longest-match
# logic in match_keywords can distinguish e.g. "reset-left" from "reset".
NORMALIZATIONS = {
    "hand over":     "handover",
    "and over":      "handover",
    "in sert":       "insert",
    "re set":        "reset",
    "re lease":      "release",
    # Per-arm resets
    "reset left":    "reset-left",
    "reset right":   "reset-right",
    # Camera stream selection
    "camera left":   "camera-left",
    "camera right":  "camera-right",
    "camera off":    "camera-off",
    "camera of":     "camera-off",
    # Episode annotation aliases
    "mark success":  "success",
    "mark partial":  "partial",
    "mark failure":  "failure",
    "episode success": "success",
    "episode partial": "partial",
    "episode failure": "failure",
    # Stats alias
    "stats":         "statistics",
}


# ── Message types ─────────────────────────────────────────────────────────────

class AnnotationType(IntEnum):
    COMMAND       = 0
    INTENT_LABEL  = 1
    QUALITY_SCORE = 2


@dataclass
class VoiceAnnotation:
    timestamp:  float
    label:      str
    atype:      AnnotationType
    confidence: float
    score:      float = -1.0


# ── Keyword matching ──────────────────────────────────────────────────────────

def _normalize(text: str) -> str: # collapse spoken variants and mishearings to canonical forms
    t = text.lower().strip().rstrip(".!?,;:")
    for spoken, canonical in NORMALIZATIONS.items():
        t = t.replace(spoken, canonical)
    return t


def match_keywords(text: str, whisper_confidence: float) -> VoiceAnnotation | None:
    lower = _normalize(text)
    ts    = time.time()

    # Quality score: "quality 7" or "quality seven"
    digit_map = {"zero":0,"one":1,"two":2,"three":3,"four":4,
                 "five":5,"six":6,"seven":7,"eight":8,"nine":9,"ten":10}
    if QUALITY_PREFIX in lower:
        words = lower.split()
        for i, w in enumerate(words):
            if w == QUALITY_PREFIX and i + 1 < len(words):
                raw = words[i + 1]
                val = int(raw) if raw.isdigit() else digit_map.get(raw, -1)
                if 0 <= val <= 10:
                    return VoiceAnnotation(ts, f"quality_{val}", AnnotationType.QUALITY_SCORE, 1.0, float(val))

    # Commands — substring match, longest wins
    best, best_len = None, 0
    for kw in COMMANDS:
        if kw in lower and len(kw) > best_len:
            best, best_len = VoiceAnnotation(ts, kw, AnnotationType.COMMAND, whisper_confidence), len(kw)
    if best:
        return best

    # Intent labels — hyphenated variants first (longer = more specific)
    for kw in sorted(INTENT_LABELS, key=len, reverse=True):
        if kw.replace("-", " ") in lower or kw in lower:
            return VoiceAnnotation(ts, kw, AnnotationType.INTENT_LABEL, whisper_confidence)

    return None


def logprob_to_confidence(avg_logprob: float) -> float: # sigmoid centered at -0.7, gentler than prior linear remap
    x = (avg_logprob + 0.7) * 2.0
    return float(1.0 / (1.0 + np.exp(-x)))


# ── UDP sender ────────────────────────────────────────────────────────────────

class UDPSender:
    def __init__(self, host: str, port: int):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(self, ann: VoiceAnnotation) -> None: # pack annotation as JSON and transmit
        payload = json.dumps({
            "timestamp":  ann.timestamp,
            "label":      ann.label,
            "type":       int(ann.atype),
            "confidence": ann.confidence,
            "score":      ann.score,
        }).encode()
        self.sock.sendto(payload, self.addr)
        print(f"[SENT] {ann.atype.name:14s} | {ann.label:20s} | conf={ann.confidence:.2f}"
              + (f" | score={ann.score:.0f}" if ann.atype == AnnotationType.QUALITY_SCORE else ""))

    def close(self) -> None:
        self.sock.close()


# ── Device helpers ────────────────────────────────────────────────────────────

def list_audio_devices(pa: pyaudio.PyAudio) -> None:
    print("Available input devices:")
    for i in range(pa.get_device_count()):
        info = pa.get_device_info_by_index(i)
        if info["maxInputChannels"] > 0:
            print(f"  [{i}] {info['name']}")


def find_vive_device(pa: pyaudio.PyAudio) -> int | None:
    for i in range(pa.get_device_count()):
        info = pa.get_device_info_by_index(i)
        if info["maxInputChannels"] > 0 and "VIVE" in info["name"].upper():
            print(f"Auto-selected device [{i}]: {info['name']}")
            return i
    print("Warning: Vive microphone not found, falling back to system default.")
    return None


# ── Audio capture thread ──────────────────────────────────────────────────────

class AudioCapture(threading.Thread):
    """
    Continuously reads mic chunks, runs VAD, segments speech with pre/post-roll padding,
    and pushes complete segments onto an output queue. Recording is independent of ASR
    so back-to-back commands are not lost while Whisper is working.
    """

    def __init__(self, segment_q: queue.Queue, device_index: int | None, stop_event: threading.Event):
        super().__init__(daemon=True)
        self.segment_q  = segment_q
        self.stop_event = stop_event

        self.vad = load_silero_vad()
        self.vad.eval()

        self.pa     = pyaudio.PyAudio()
        self.stream = self.pa.open(
            rate=SAMPLE_RATE, channels=CHANNELS,
            format=pyaudio.paInt16, input=True,
            frames_per_buffer=CHUNK_SAMPLES,
            input_device_index=device_index,
        )

    def _vad_prob(self, chunk: np.ndarray) -> float: # run VAD on a single 512-sample chunk
        t = torch.from_numpy(chunk.astype(np.float32) / 32768.0)
        with torch.no_grad():
            return self.vad(t, SAMPLE_RATE).item()

    def run(self) -> None:
        pre_roll       = deque(maxlen=PRE_ROLL_CHUNKS)
        active_frames: list[np.ndarray] = []
        recording      = False
        silence        = 0
        post_roll_left = 0
        max_chunks     = int(MAX_RECORD_SEC / CHUNK_SEC)

        while not self.stop_event.is_set():
            try:
                raw = self.stream.read(CHUNK_SAMPLES, exception_on_overflow=False)
            except OSError as e:
                print(f"[AUDIO] read error: {e}")
                continue
            chunk = np.frombuffer(raw, dtype=np.int16)
            prob  = self._vad_prob(chunk)

            if not recording:
                pre_roll.append(chunk)
                if prob > VAD_THRESHOLD: # speech onset — flush pre-roll into active buffer
                    active_frames = list(pre_roll)
                    active_frames.append(chunk)
                    recording      = True
                    silence        = 0
                    post_roll_left = POST_ROLL_CHUNKS
            else:
                active_frames.append(chunk)
                if prob > VAD_THRESHOLD:
                    silence        = 0
                    post_roll_left = POST_ROLL_CHUNKS
                else:
                    silence += 1
                    if post_roll_left > 0:
                        post_roll_left -= 1

                hit_silence = silence >= SILENCE_CHUNKS and post_roll_left == 0
                hit_max     = len(active_frames) >= max_chunks

                if hit_silence or hit_max:
                    audio = np.concatenate(active_frames).astype(np.float32) / 32768.0
                    duration = len(audio) / SAMPLE_RATE
                    if duration >= MIN_RECORD_SEC:
                        try:
                            self.segment_q.put_nowait(audio)
                        except queue.Full:
                            print("[AUDIO] segment queue full — dropping oldest")
                            try:
                                self.segment_q.get_nowait()
                                self.segment_q.put_nowait(audio)
                            except queue.Empty:
                                pass
                    active_frames  = []
                    recording      = False
                    silence        = 0
                    post_roll_left = 0
                    pre_roll.clear()

    def close(self) -> None:
        try:
            self.stream.stop_stream()
            self.stream.close()
        finally:
            self.pa.terminate()


# ── ASR thread ────────────────────────────────────────────────────────────────

class Transcriber(threading.Thread):
    """Pulls audio segments off the queue, transcribes with Whisper, sends matches over UDP."""

    def __init__(self, segment_q: queue.Queue, sender: UDPSender,
                 whisper: WhisperModel, stop_event: threading.Event):
        super().__init__(daemon=True)
        self.segment_q  = segment_q
        self.sender     = sender
        self.whisper    = whisper
        self.stop_event = stop_event

    def run(self) -> None:
        while not self.stop_event.is_set():
            try:
                audio = self.segment_q.get(timeout=0.2)
            except queue.Empty:
                continue

            t0 = time.time()
            segs, _ = self.whisper.transcribe(
                audio,
                language=WHISPER_LANGUAGE,
                beam_size=1,                       # closed vocab, short utterances — beam > 1 wastes latency
                temperature=0.0,
                initial_prompt=INITIAL_PROMPT,
                condition_on_previous_text=False,  # critical: prevents drift / repetition across segments
                vad_filter=False,
            )
            segments = list(segs)
            asr_ms   = (time.time() - t0) * 1000.0

            if not segments:
                continue

            no_speech = max(s.no_speech_prob for s in segments)
            if no_speech > NO_SPEECH_THRESHOLD:
                print(f"[SKIP] no_speech_prob={no_speech:.2f}  ({asr_ms:.0f}ms)")
                continue

            text        = " ".join(s.text.strip() for s in segments).strip()
            avg_logprob = sum(s.avg_logprob for s in segments) / len(segments)
            if avg_logprob < LOGPROB_REJECT:
                print(f"[SKIP] avg_logprob={avg_logprob:.2f} below threshold  ({asr_ms:.0f}ms)")
                continue
            whisper_conf = logprob_to_confidence(avg_logprob)
            if not text:
                continue

            print(f"[ASR ] {text!r}  no_speech={no_speech:.2f}  conf={whisper_conf:.2f}  ({asr_ms:.0f}ms)")
            ann = match_keywords(text, whisper_conf)
            if ann:
                self.sender.send(ann)
            else:
                print(f"[SKIP] no keyword match")


# ── Main ──────────────────────────────────────────────────────────────────────

def run(host: str, port: int, device: str, device_index: int | None) -> None:
    print(f"Loading Whisper ({WHISPER_MODEL}) on {device}...")
    compute_type = "int8_float16" if device == "cuda" else "int8"
    whisper = WhisperModel(WHISPER_MODEL, device=device, compute_type=compute_type)

    print("Loading VAD and opening mic...")
    if device_index is None:
        pa_tmp = pyaudio.PyAudio()
        device_index = find_vive_device(pa_tmp)
        pa_tmp.terminate()

    stop_event = threading.Event()
    segment_q: queue.Queue = queue.Queue(maxsize=8)

    sender   = UDPSender(host, port)
    capture  = AudioCapture(segment_q, device_index, stop_event)
    asr      = Transcriber(segment_q, sender, whisper, stop_event)

    capture.start()
    asr.start()

    ui_cmds     = {"home", "success", "partial", "failure",
                   "reset-left", "reset-right", "statistics", "viewpoint", "camera-off"}
    state_cmds  = COMMANDS - ui_cmds
    print(f"Ready. Sending to {host}:{port}\n"
          f"Vocabulary — state:    {state_cmds}\n"
          f"             UI/annot: {ui_cmds}\n"
          f"             intents:  {INTENT_LABELS}\n"
          f"             quality:  'quality <0-10>'\n")

    try:
        while not stop_event.is_set():
            time.sleep(0.2)
    except KeyboardInterrupt:
        print("\nShutting down.")
    finally:
        stop_event.set()
        capture.join(timeout=2.0)
        asr.join(timeout=2.0)
        capture.close()
        sender.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Voice annotator for teleoperation pipeline")
    parser.add_argument("--host",         default="127.0.0.1")
    parser.add_argument("--port",         default=7778, type=int)
    parser.add_argument("--device",       default="cuda", choices=["cpu", "cuda"])
    parser.add_argument("--device-index", default=None, type=int, help="Audio input device index")
    parser.add_argument("--list-devices", action="store_true", help="List audio input devices and exit")
    args = parser.parse_args()

    if args.list_devices:
        pa = pyaudio.PyAudio()
        list_audio_devices(pa)
        pa.terminate()
        sys.exit(0)

    run(args.host, args.port, args.device, args.device_index)