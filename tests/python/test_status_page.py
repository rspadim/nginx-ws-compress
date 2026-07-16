"""
Test the /ws_deflate_status endpoint and compression metrics.
"""
import pytest
import httpx

pytestmark = pytest.mark.asyncio

STATUS_URL = "http://127.0.0.1:8090/ws_deflate_status"


async def test_status_page_returns_json(nginx_server):
    """Status page returns valid JSON with expected fields."""
    async with httpx.AsyncClient() as http:
        resp = await http.get(STATUS_URL, timeout=5.0)
        assert resp.status_code == 200
        data = resp.json()
        ws = data.get("ws_deflate", {})
        assert ws.get("status") == "active"
        assert "connections_total" in ws
        assert "connections_active" in ws
        assert "frames_processed" in ws
        assert "bytes_uncompressed" in ws
        assert "bytes_compressed" in ws
        assert "compression_ratio_pct" in ws


async def test_status_after_websocket_connection(nginx_server):
    """After a WebSocket connection, counters should be non-zero."""
    from ws_client import WSTestClient

    # Connect and exchange messages to generate metrics
    client = WSTestClient("ws://127.0.0.1:8090/ws")
    await client.connect()
    await client.send_text("status test")
    await client.recv_text()
    await client.close()

    # Check status page
    async with httpx.AsyncClient() as http:
        resp = await http.get(STATUS_URL, timeout=5.0)
        assert resp.status_code == 200
        data = resp.json()
        ws = data.get("ws_deflate", {})

        print(f"\n  Status after one connection:")
        print(f"    connections_total:   {ws.get('connections_total')}")
        print(f"    connections_active:  {ws.get('connections_active')}")
        print(f"    frames_processed:    {ws.get('frames_processed')}")

        # At minimum, the connection was recorded
        assert ws.get("connections_total", 0) >= 1, (
            "Expected at least 1 connection"
        )
