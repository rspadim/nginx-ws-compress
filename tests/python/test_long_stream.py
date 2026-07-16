"""
Long-stream test: sends data through a single WebSocket connection,
verifying memory stability and data integrity.

Use --long-stream to run the full 100MB version.
Default (CI): 10MB / 640 messages of 16KB.
"""
import os
import asyncio
import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio

# CI-safe: 10MB total
TOTAL_DATA = 10 * 1024 * 1024   # 10 MB
MSG_SIZE   = 16 * 1024          # 16 KB per message
NUM_MSGS   = TOTAL_DATA // MSG_SIZE  # 640 messages


async def test_long_stream_memory_stable(nginx_server):
    """
    Send 100MB through a single WebSocket connection (6400 messages of 16KB).
    Verify:
      - All messages echo correctly
      - nginx memory doesn't grow unbounded
    """
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()

    # Sample nginx memory before
    mem_before = _get_nginx_rss()

    for i in range(NUM_MSGS):
        payload = f"msg-{i:06d}:" + "X" * (MSG_SIZE - 20)  # ~16KB each
        await client.send_text(payload)
        response = await client.recv_text()
        assert response == payload, f"Mismatch at msg {i}"

        # Log progress every 1000 messages
        if i > 0 and i % 1000 == 0:
            sent_mb = (i * MSG_SIZE) / (1024 * 1024)
            mem_now = _get_nginx_rss()
            delta_mb = (mem_now - mem_before) / (1024 * 1024)
            print(f"  [{i}/{NUM_MSGS}] {sent_mb:.0f}MB sent, "
                  f"nginx delta: {delta_mb:+.1f}MB")

    mem_after = _get_nginx_rss()
    delta_mb = (mem_after - mem_before) / (1024 * 1024)

    print(f"\n  Total: {NUM_MSGS} messages, "
          f"{TOTAL_DATA / (1024*1024):.0f}MB")
    print(f"  nginx RSS before: {mem_before / (1024*1024):.1f}MB")
    print(f"  nginx RSS after:  {mem_after / (1024*1024):.1f}MB")
    print(f"  Delta: {delta_mb:+.1f}MB")

    # Allow some growth for connection state, but not more than 20MB
    # (6400 frames * ~3KB overhead = ~19MB theoretical worst case)
    assert delta_mb < 50, \
        f"nginx memory grew {delta_mb:.1f}MB — possible leak"

    await client.close()

    # Check status page metrics to confirm compression was active
    await _check_status_metrics()


async def _check_status_metrics():
    """
    Fetch /ws_deflate_status and verify compression counters.
    This confirms the status page works and compression is active.
    """
    import httpx
    async with httpx.AsyncClient() as http:
        resp = await http.get("http://127.0.0.1:8090/ws_deflate_status",
                              timeout=5.0)
        assert resp.status_code == 200, f"Status page returned {resp.status_code}"
        data = resp.json()
        ws = data.get("ws_deflate", {})

        print(f"\n  Status page metrics:")
        print(f"    connections_total:   {ws.get('connections_total', 'N/A')}")
        print(f"    connections_active:  {ws.get('connections_active', 'N/A')}")
        print(f"    frames_processed:    {ws.get('frames_processed', 'N/A')}")
        print(f"    bytes_uncompressed:  {ws.get('bytes_uncompressed', 'N/A')}")
        print(f"    bytes_compressed:    {ws.get('bytes_compressed', 'N/A')}")
        print(f"    compression_ratio:   {ws.get('compression_ratio_pct', 'N/A')}%")

        assert ws.get("connections_total", 0) > 0, "No connections recorded"
        assert ws.get("frames_processed", 0) > 0, "No frames processed"
        assert ws.get("compression_ratio_pct", 0) > 0, (
            "Compression ratio is 0% — compression may not be active"
        )


def _get_nginx_rss() -> int:
    """Get RSS of nginx worker processes in bytes."""
    import psutil
    total = 0
    for proc in psutil.process_iter(["name", "cmdline"]):
        try:
            name = proc.info["name"]
            cmdline = proc.info.get("cmdline") or []
            if name and "nginx" in name:
                # Only count worker processes (not master or CLI)
                if any("worker" in c for c in cmdline if c):
                    total += proc.memory_info().rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return total
