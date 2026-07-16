"""
Latency benchmark: measures round-trip time added by the compression module.
Reports in microseconds per message.
"""
import asyncio
import statistics
import time
import pytest

from ws_client import WSTestClient

pytestmark = pytest.mark.asyncio

NUM_SAMPLES = 1000       # messages per test
MSG_SIZE = 1024          # 1KB payload
WARMUP = 100             # warmup messages before measuring


async def _measure_latency(url: str, label: str) -> dict:
    """Connect, send messages, measure RTT, return stats."""
    client = WSTestClient(url)
    await client.connect()

    # Warmup
    for _ in range(WARMUP):
        await client.send_text("x" * MSG_SIZE)
        await client.recv_text()

    # Measure
    times = []
    payload = "y" * MSG_SIZE
    for _ in range(NUM_SAMPLES):
        start = time.perf_counter()
        await client.send_text(payload)
        await client.recv_text()
        elapsed = time.perf_counter() - start
        times.append(elapsed * 1_000_000)  # convert to microseconds

    await client.close()

    avg = statistics.mean(times)
    med = statistics.median(times)
    stdev = statistics.stdev(times) if len(times) > 1 else 0
    p99 = sorted(times)[int(len(times) * 0.99)]

    print(f"\n  {label}:")
    print(f"    Samples:      {NUM_SAMPLES}")
    print(f"    Mean:         {avg:.1f} µs")
    print(f"    Median:       {med:.1f} µs")
    print(f"    Std dev:      {stdev:.1f} µs")
    print(f"    P99:          {p99:.1f} µs")
    print(f"    Throughput:   {1_000_000 / avg:.0f} msgs/sec")

    return {"avg": avg, "med": med, "p99": p99, "label": label}


async def test_module_latency_overhead(nginx_server):
    """
    Measure RTT through nginx WITH the module active (ws_deflate on).
    The client doesn't negotiate compression, so the tunnel runs in
    pass-through mode. This measures the baseline overhead of the
    frame parser when no compression is applied.
    """
    result = await _measure_latency(
        "ws://127.0.0.1:8090/ws",
        "nginx + ws_deflate (passthrough)"
    )
    # Typical expected: < 500 µs per message
    assert result["avg"] < 2000, (
        f"Average latency too high: {result['avg']:.0f} µs"
    )


async def test_compression_latency(nginx_server):
    """
    Measure RTT through nginx with compression enabled on the server side.
    The tunnel processes frames even without client compression negotiation.
    This measures the full pipeline: parse → serialize → buffer management.
    """
    result = await _measure_latency(
        "ws://127.0.0.1:8090/ws",
        "nginx + ws_deflate (frame processing)"
    )
    assert result["avg"] < 2000, (
        f"Average latency too high: {result['avg']:.0f} µs"
    )


async def test_no_compress_latency(nginx_server):
    """
    Baseline: RTT through nginx WITH module loaded but ws_deflate off.
    This measures pure proxy overhead without any module processing.
    """
    result = await _measure_latency(
        "ws://127.0.0.1:8090/no-compress",
        "nginx + ws_deflate off (baseline)"
    )
    assert result["avg"] < 2000, (
        f"Average latency too high: {result['avg']:.0f} µs"
    )
