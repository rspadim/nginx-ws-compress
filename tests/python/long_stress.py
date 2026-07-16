"""
Long stress test for nginx ws_deflate module.
Runs for N minutes with M concurrent connections, monitoring memory.

Usage: python long_stress.py [--duration 30] [--connections 5]
"""
import asyncio
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

try:
    import psutil
except ImportError:
    psutil = None

sys.path.insert(0, str(Path(__file__).parent))
from ws_client import WSTestClient


MSG_SIZE = 16384  # 16KB per message
REPORT_INTERVAL = 30  # seconds between memory reports


def get_nginx_mem() -> int:
    """Total RSS of nginx worker processes in bytes."""
    if psutil is None:
        return 0
    total = 0
    for proc in psutil.process_iter(["name", "cmdline"]):
        try:
            name = proc.info["name"]
            cmdline = proc.info.get("cmdline") or []
            if name and "nginx" in name:
                if any("worker" in c for c in cmdline if c):
                    total += proc.memory_info().rss
        except Exception:
            continue
    return total


async def stress_client(client_id: int, duration: float):
    """Keep sending messages for the full duration."""
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    msg_count = 0
    start = time.time()
    try:
        while time.time() - start < duration:
            payload = f"s-{client_id}-{msg_count}:" + "Z" * (MSG_SIZE - 20)
            await client.send_text(payload)
            resp = await client.recv_text()
            assert resp == payload, f"Data mismatch at {client_id}/{msg_count}"
            msg_count += 1
            await asyncio.sleep(0.005)
    except Exception as e:
        print(f"  [C{client_id}] Error after {msg_count} msgs: {e}")
    finally:
        await client.close()
    return msg_count


async def report_loop(duration: float, mem_start: int):
    """Report memory every REPORT_INTERVAL seconds."""
    end = time.time() + duration
    start = time.time()
    while time.time() < end:
        await asyncio.sleep(REPORT_INTERVAL)
        mem_now = get_nginx_mem()
        delta_mb = (mem_now - mem_start) / 1024 / 1024
        elapsed = int(time.time() - start)
        print(f"  [{elapsed}s] nginx RSS: {mem_now / 1024 / 1024:.1f} MB "
              f"(delta: {delta_mb:+.2f} MB)")


async def main():
    parser = argparse.ArgumentParser(description="Long stress test")
    parser.add_argument("--duration", type=int, default=30, help="Duration in minutes")
    parser.add_argument("--connections", type=int, default=5, help="Concurrent connections")
    args = parser.parse_args()

    duration_s = args.duration * 60
    connections = args.connections

    print(f"=== Long Stress Test ===")
    print(f"Duration: {duration_s}s ({args.duration}min)")
    print(f"Connections: {connections}")
    print(f"Message size: {MSG_SIZE}B")

    # Start backend if not running
    venv_python = Path(__file__).parent / "venv" / "bin" / "python3"
    if not venv_python.exists():
        venv_python = Path(__file__).parent / "venv" / "bin" / "python"
    if not venv_python.exists():
        venv_python = Path("python3")

    subprocess.run(["pkill", "-9", "nginx"], capture_output=True)
    subprocess.run(["pkill", "-9", "uvicorn"], capture_output=True)
    time.sleep(2)

    backend = subprocess.Popen(
        [str(venv_python), "-m", "uvicorn", "ws_backend:app",
         "--host", "127.0.0.1", "--port", "9001", "--log-level", "error"],
        cwd=str(Path(__file__).parent),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    time.sleep(2)

    # Start nginx
    prefix = "/tmp/nginx-ws-stress"
    os.makedirs(f"{prefix}/logs", exist_ok=True)
    for d in ["client_body_temp", "proxy_temp"]:
        os.makedirs(f"{prefix}/{d}", exist_ok=True)

    nginx_conf = str(Path(__file__).parent / "nginx.conf")

    # Verify config before starting
    result = subprocess.run(
        ["/usr/local/nginx/sbin/nginx", "-c", nginx_conf, "-p", prefix, "-t"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"nginx config test failed:\n{result.stderr}")
        sys.exit(1)
    print(f"nginx config test: OK")

    result = subprocess.run(
        ["/usr/local/nginx/sbin/nginx", "-c", nginx_conf, "-p", prefix],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"nginx start failed:\n{result.stderr}")
        sys.exit(1)

    # Wait for nginx to be ready
    for i in range(10):
        r = subprocess.run(
            ["curl", "-s", "-o", "/dev/null", "-w", "%{http_code}",
             "http://127.0.0.1:8090/"],
            capture_output=True, text=True, timeout=5,
        )
        if r.stdout.strip() != "000":
            print(f"nginx ready (HTTP {r.stdout.strip()})")
            break
        time.sleep(1)
    else:
        print("nginx failed to start in time")
        sys.exit(1)

    mem_start = get_nginx_mem()
    print(f"nginx RSS start: {mem_start / 1024 / 1024:.1f} MB\n")

    # Run stress
    tasks = [stress_client(i, duration_s) for i in range(connections)]
    reporter = asyncio.create_task(report_loop(duration_s, mem_start))
    results = await asyncio.gather(*tasks)
    reporter.cancel()

    # Results
    total_msgs = sum(results)
    total_data = total_msgs * MSG_SIZE
    mem_end = get_nginx_mem()
    delta_mb = (mem_end - mem_start) / 1024 / 1024

    print(f"\n=== Results ===")
    print(f"Total messages: {total_msgs}")
    print(f"Total data: {total_data / 1024 / 1024:.1f} MB")
    print(f"nginx RSS end:   {mem_end / 1024 / 1024:.1f} MB")
    print(f"nginx RSS delta: {delta_mb:+.2f} MB")
    if connections > 0:
        print(f"Per-connection overhead: {delta_mb / connections:.2f} MB")

    # Cleanup
    subprocess.run(
        ["/usr/local/nginx/sbin/nginx", "-s", "stop", "-p", prefix],
        capture_output=True,
    )
    backend.terminate()
    backend.wait()

    # Summary for GitHub Actions
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "w") as f:
            f.write("## Long Stress Test Results\n")
            f.write("| Metric | Value |\n")
            f.write("|---|---|\n")
            f.write(f"| Duration | {args.duration} min |\n")
            f.write(f"| Connections | {connections} |\n")
            f.write(f"| Total messages | {total_msgs} |\n")
            f.write(f"| Total data | {total_data / 1024 / 1024:.1f} MB |\n")
            f.write(f"| Memory delta | {delta_mb:+.2f} MB |\n")
            status = "PASS" if delta_mb <= 100 else "FAIL"
            f.write(f"| Status | {status} |\n")

    if delta_mb > 100:
        print(f"FAIL: memory grew {delta_mb:.1f}MB — possible leak")
        sys.exit(1)
    else:
        print(f"PASS: memory stable (delta {delta_mb:+.2f}MB)")


if __name__ == "__main__":
    asyncio.run(main())
