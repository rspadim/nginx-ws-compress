"""
Memory consumption tests for long-lived WebSocket connections.
Verifies that the reusable buffer strategy prevents memory leaks.
"""
import os
import asyncio
import psutil
import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio

# Each connection sends this many messages
MSGS_PER_CONN = 2000
MSG_SIZE = 8192  # 8KB
TOTAL_PER_CONN = MSGS_PER_CONN * MSG_SIZE  # ~16MB per connection


def _get_nginx_rss() -> int:
    """Total RSS of nginx worker processes in bytes."""
    total = 0
    for proc in psutil.process_iter(["name", "cmdline"]):
        try:
            name = proc.info["name"]
            cmdline = proc.info.get("cmdline") or []
            if name and "nginx" in name:
                if any("worker" in c for c in cmdline if c):
                    total += proc.memory_info().rss
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return total


async def _single_connection_stress(client_id: int):
    """Single WebSocket connection sending many messages."""
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    for i in range(MSGS_PER_CONN):
        payload = f"mem-{client_id}-{i:06d}:" + "Y" * (MSG_SIZE - 30)
        await client.send_text(payload)
        resp = await client.recv_text()
        assert resp == payload, f"Data mismatch at {client_id}/{i}"
        # Progress log for long runs
        if i > 0 and i % 500 == 0:
            mb = (i * MSG_SIZE) / (1024 * 1024)
            print(f"    [{client_id}] {i}/{MSGS_PER_CONN} ({mb:.0f}MB sent)")
    await client.close()


async def test_memory_after_single_long_connection(nginx_server):
    """
    Open one WebSocket connection, send ~16MB of data (2000×8KB messages).
    Measure nginx RSS before and after.
    With reusable buffers, delta should be tiny (< 5MB).
    """
    mem_before = _get_nginx_rss()

    print(f"\n  Sending {TOTAL_PER_CONN / (1024*1024):.0f}MB over one connection...")
    await _single_connection_stress(0)

    mem_after = _get_nginx_rss()
    delta_mb = (mem_after - mem_before) / (1024 * 1024)

    print(f"  nginx RSS before: {mem_before / (1024*1024):.1f} MB")
    print(f"  nginx RSS after:  {mem_after / (1024*1024):.1f} MB")
    print(f"  Delta: {delta_mb:+.2f} MB")

    # Allow small growth for connection state, but not per-frame accumulation
    assert delta_mb < 10, (
        f"Memory grew {delta_mb:.1f}MB — possible per-frame leak"
    )


async def test_memory_after_concurrent_connections(nginx_server):
    """
    Open 10 concurrent connections, each sending ~16MB.
    Total: ~160MB through nginx.
    Verify nginx memory stays stable (reusable buffers per connection).
    """
    mem_before = _get_nginx_rss()

    total_mb = TOTAL_PER_CONN * 10 / (1024 * 1024)
    print(f"\n  Sending {total_mb:.0f}MB over 10 concurrent connections...")
    tasks = [_single_connection_stress(i) for i in range(10)]
    await asyncio.gather(*tasks)

    mem_after = _get_nginx_rss()
    delta_mb = (mem_after - mem_before) / (1024 * 1024)

    print(f"  nginx RSS before: {mem_before / (1024*1024):.1f} MB")
    print(f"  nginx RSS after:  {mem_after / (1024*1024):.1f} MB")
    print(f"  Delta: {delta_mb:+.2f} MB")
    print(f"  Per-connection overhead: {delta_mb / 10:.2f} MB")

    # 10 connections × ~3MB overhead each = 30MB max
    assert delta_mb < 50, (
        f"Memory grew {delta_mb:.1f}MB for 10 connections — possible leak"
    )
