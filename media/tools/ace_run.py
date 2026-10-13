"""ACE-Step 1.5 (turbo, all-in-one checkpoint) through the ComfyUI API; the server must be running on port 8188.
Instrumental pieces for Juraic Field. Files come out in the folder output/audio of ComfyUI as flac.

usage: python ace_run.py <prefix> <bpm> <seconds> "<key, e.g. D minor>" "<tags>" [seed ...]
"""
import json
import sys
import time
import urllib.request

HOST = "http://127.0.0.1:8188"
prefix, bpm, seconds, key, tags = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4], sys.argv[5]
seeds = [int(v) for v in sys.argv[6:]] or [1]

ids = []
for seed in seeds:
    wf = {
        "1": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "ace_step_1.5_turbo_aio.safetensors"}},
        "2": {"class_type": "ModelSamplingAuraFlow", "inputs": {"model": ["1", 0], "shift": 3}},
        "3": {"class_type": "TextEncodeAceStepAudio1.5", "inputs": {
            "clip": ["1", 1], "tags": tags, "lyrics": "[Instrumental]", "seed": seed, "bpm": bpm, "duration": seconds,
            "timesignature": "4", "language": "en", "keyscale": key, "generate_audio_codes": True,
            "cfg_scale": 2.0, "temperature": 0.85, "top_p": 0.9, "top_k": 0, "min_p": 0.0}},
        "4": {"class_type": "ConditioningZeroOut", "inputs": {"conditioning": ["3", 0]}},
        "5": {"class_type": "EmptyAceStep1.5LatentAudio", "inputs": {"seconds": seconds, "batch_size": 1}},
        "6": {"class_type": "KSampler", "inputs": {"model": ["2", 0], "positive": ["3", 0], "negative": ["4", 0], "latent_image": ["5", 0],
                                                   "seed": seed, "steps": 8, "cfg": 1.0, "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0}},
        "7": {"class_type": "VAEDecodeAudio", "inputs": {"samples": ["6", 0], "vae": ["1", 2]}},
        "8": {"class_type": "SaveAudio", "inputs": {"audio": ["7", 0], "filename_prefix": "audio/%s_s%d" % (prefix, seed)}},
    }
    req = urllib.request.Request(HOST + "/prompt", data=json.dumps({"prompt": wf}).encode(), headers={"Content-Type": "application/json"})
    try:
        ids.append((seed, json.load(urllib.request.urlopen(req))["prompt_id"]))
    except urllib.error.HTTPError as e:
        print("REJECTED:", e.read().decode()[:3000])
        sys.exit(1)

t0 = time.time()
for seed, pid in ids:
    while True:
        h = json.load(urllib.request.urlopen("%s/history/%s" % (HOST, pid)))
        if pid in h:
            break
        time.sleep(3)
    st = h[pid]["status"]
    print("seed", seed, "status:", st["status_str"], "| %.0f s since the start" % (time.time() - t0))
    for m in st.get("messages", []):
        if m[0] == "execution_error":
            print("ERROR:", m[1].get("node_type"), m[1].get("exception_message", "")[:1500])
    for o in h[pid].get("outputs", {}).values():
        for a in o.get("audio", []):
            print("audio:", a.get("subfolder", ""), a["filename"])
