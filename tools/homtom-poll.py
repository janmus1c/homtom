#!/usr/bin/env python3
"""Poll the device status endpoint and append JSON lines to .runtime/status.jsonl (stand-in for a Home Assistant REST sensor).
Usage: tools/homtom-poll.py [interval-seconds] [count]"""
import json, pathlib, sys, time, urllib.request

interval = float(sys.argv[1]) if len(sys.argv) > 1 else 30
count = int(sys.argv[2]) if len(sys.argv) > 2 else 0
out = pathlib.Path(__file__).resolve().parent.parent / ".runtime" / "status.jsonl"
out.parent.mkdir(exist_ok=True)
n = 0
while True:
    try:
        raw = urllib.request.urlopen("http://192.168.1.10:8080/", timeout=10).read().decode()
        doc = json.loads(raw[raw.index("{"):])
        doc["t"] = int(time.time())
        out.open("a").write(json.dumps(doc) + "\n")
        print(doc)
    except Exception as e:
        print("poll failed:", e)
    n += 1
    if count and n >= count:
        break
    time.sleep(interval)
