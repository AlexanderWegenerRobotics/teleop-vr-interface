# Voice annotator

Push-free voice command sidecar for the teleop interface. Listens on the Vive
headset microphone, segments speech with silero-vad, transcribes with
faster-whisper (`small.en`), matches against a closed vocabulary, and sends each
match as one JSON UDP datagram to `127.0.0.1:7778`.

`UVoiceAnnotatorComponent` launches it on `BeginPlay` and terminates it on
`EndPlay`, so it is not normally started by hand.

## Environment

The env name matters: `UVoiceAnnotatorComponent::PythonExe` defaults to

```
C:\Users\ceti\miniconda3\envs\voice-control\python.exe
```

so recreating it under a different name means editing that property on the
pawn as well.

```bat
conda create -n voice-control python=3.11 -y
conda activate voice-control
pip install -r requirements.txt
```

Python 3.11 rather than the newest available: `pyaudio` and `ctranslate2` are
both binary wheels, and the newest CPython is routinely the one they have not
shipped for yet.

### If `pip install pyaudio` fails to build

It needs the PortAudio binaries. On Windows either use a prebuilt wheel, or:

```bat
conda install -c conda-forge pyaudio
```

### GPU

`--device cuda` (the default) selects CUDA for **Whisper**, which runs on
CTranslate2 and not on torch. CTranslate2 needs cuBLAS and cuDNN DLLs it does
not bundle; see the commented block in `requirements.txt` if it cannot find
them. `--device cpu` works and is fast enough for short commands.

torch is only ever used for the VAD — a tiny model over 512-sample chunks — so
a CPU torch build is fine regardless of which device Whisper uses.

## Checking it works, without the interface

```bat
conda activate voice-control
python voice_annotator.py --list-devices
python voice_annotator.py --device-index <n>          [--device cpu]
```

It prints `Auto-selected device [n]: ...` if it finds a device with "VIVE" in
the name, and otherwise falls back to the system default with a warning. On a
match you get one `[SENT]` line per recognised command.

To watch the datagrams the interface would receive, from another shell:

```bat
python -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('127.0.0.1',7778));print('listening');[print(s.recv(2048)) for _ in iter(int,1)]"
```

Note the interface binds that port itself when it is running, so only one of
the two can listen at a time.

## Vocabulary

| kind | words |
|---|---|
| state | `start` `stop` `reset` `pause` `engage` |
| UI / annotation | `home` `success` `partial` `failure` `reset left` `reset right` `statistics` `settings` `viewpoint` `camera left` `camera right` `camera off` |
| intent labels | `grasp` `insert` `handover` `handover left` `handover right` `place` `release` |
| quality | `quality <0-10>`, digit or word |

`NORMALIZATIONS` in the script maps common mishearings onto these
(`"hand over"` → `handover`, `"stats"` → `statistics`, and so on). Add to that
table rather than loosening the thresholds when a word is missed consistently.

`INITIAL_PROMPT` biases Whisper's language model toward this vocabulary and is
worth updating in step with it — it is a substantial accuracy gain on short
noisy utterances.

## Tuning

| constant | effect |
|---|---|
| `VAD_THRESHOLD` 0.4 | lower catches plosive-initial words, raises false segments |
| `SILENCE_CHUNKS` 15 | ~480 ms of silence ends a segment; raise if words get cut |
| `PRE_ROLL_CHUNKS` 10 | ~320 ms kept before onset, preserves the first phoneme |
| `MAX_RECORD_SEC` 6 | hard cap per segment |
| `LOGPROB_REJECT` -1.0 | rejects low-confidence transcriptions outright |
| `NO_SPEECH_THRESHOLD` 0.6 | rejects segments Whisper thinks are not speech |

Every rejection prints a `[SKIP]` line with the number that caused it, so tuning
is a matter of reading the console rather than guessing.

## Packaging

This folder is staged into packaged builds as non-cooked content via
`DirectoriesToAlwaysStageAsNonUFS` in `Config/DefaultGame.ini`, and
`VoiceAnnotatorComponent` resolves `VoiceScriptPath` relative to the project
directory. So a packaged build runs its own copy of this script rather than
reaching back into the repo.

The conda interpreter is still a machine dependency and cannot be staged.
