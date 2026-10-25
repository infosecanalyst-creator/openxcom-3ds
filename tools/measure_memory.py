import os, subprocess, time, signal, sys
env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", LD_PRELOAD="/root/xcomshim/shim.so", XCOM_SCALE="2",
           XCOM_CLICKS=os.environ.get("XCOM_CLICKS", ""))
cmd = ["/root/scope/OpenXcom/build/bin/openxcom", "-data", "/root/scope/OpenXcom/bin/",
       "-user", "/root/xcom-user/", "-cfg", "/root/xcom-user/"]
p = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, cwd="/root/scope/OpenXcom/bin")
peak = 0; last = 0; t0 = time.time()
samples = []
while time.time() - t0 < float(os.environ.get("XCOM_SECS", "60")) and p.poll() is None:
    try:
        for line in open("/proc/%d/status" % p.pid):
            if line.startswith("VmHWM"): peak = int(line.split()[1]) // 1024
            if line.startswith("VmRSS"): last = int(line.split()[1]) // 1024
    except FileNotFoundError:
        break
    samples.append((round(time.time() - t0, 1), last, peak))
    time.sleep(0.5)
if p.poll() is None:
    p.send_signal(signal.SIGTERM); time.sleep(1)
    if p.poll() is None: p.kill()
err = p.stderr.read().decode(errors="replace")
print("exit:", p.returncode)
print("peak RSS MB:", peak)
print("samples (s, rss, peak):", samples[::10])
print("stderr tail:", err[-1500:])
print("log:", open("/root/xcom-user/openxcom.log").read()[-2500:] if os.path.exists("/root/xcom-user/openxcom.log") else "none")
