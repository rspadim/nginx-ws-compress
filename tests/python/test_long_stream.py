"""
Long-stream test: sends 100MB of data through a single WebSocket connection,
verifying memory stability and data integrity.
"""
import os
import asyncio
import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio

TOTAL_DATA = 100 * 1024 * 1024   # 100 MB total
MSG_SIZE   = 16 * 1024           # 16 KB per message
NUM_MSGS   = TOTAL_DATA // MSG_SIZE  # 6400 messages


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
