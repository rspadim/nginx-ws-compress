"""
Test that compression is actually active when the browser negotiates
permessage-deflate, and that ws_deflate_max_compress_len limits it.
"""
import pytest
from playwright.sync_api import sync_playwright


def _chromium_available():
    try:
        with sync_playwright() as p:
            p.chromium.launch(headless=True).close()
        return True
    except Exception:
        return False


pytestmark = [pytest.mark.skipif(not _chromium_available(),
    reason="Playwright Chromium not available")]


def test_compression_reduces_wire_size(nginx_server):
    """
    Chrome negotiates permessage-deflate automatically.
    A highly repetitive 10KB payload should be compressed to much less
    on the wire. We verify by checking nginx's debug log for
    "compressed X→Y" entries where Y < X.
    """
    from pathlib import Path

    # nginx.conf now has error_log ... debug; pointing here
    log_path = Path("/tmp/nginx-ws-test-debug/logs/error.log")
    if log_path.exists():
        log_path.write_text("")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        # Use JS .repeat() to avoid huge string in Python f-string
        result = page.evaluate("""
            async () => {
                const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                return await new Promise((resolve, reject) => {
                    ws.onopen = () => ws.send('A'.repeat(10000));
                    ws.onmessage = (e) => resolve(e.data.length);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 15000);
                });
            }
        """)
        assert result == 10000, f"Expected 10000 bytes back, got {result}"
        browser.close()

    # Check debug log for compression evidence
    log_text = log_path.read_text()
    assert "ws_deflate: tunnel installed" in log_text, (
        "Tunnel was not installed — compression cannot happen"
    )
    assert "ws_deflate: compressed" in log_text, (
        "No compression entries in debug log — tunnel may not be active"
    )

    # Print compression ratio
    for line in log_text.splitlines():
        if "ws_deflate: compressed" in line:
            parts = line.split("compressed ")[1].split("→")
            before = int(parts[0])
            after = int(parts[1].split(" ")[0])
            ratio = after / before if before > 0 else 1
            print(f"  Compression: {before} → {after} bytes (ratio: {ratio:.3f})")
            assert ratio < 0.5, (
                f"Compression ratio too high: {ratio:.3f} "
                f"(expected <0.5 for repetitive data)"
            )
            break


def test_max_compress_len_respected(nginx_server):
    """
    ws_deflate_max_compress_len 1024 should skip compression for
    payloads larger than 1KB. A 10KB message should pass through raw.
    """
    from pathlib import Path

    log_path = Path("/tmp/nginx-ws-test-debug/logs/error.log")
    if log_path.exists():
        log_path.write_text("")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        result = page.evaluate("""
            async () => {
                const ws = new WebSocket('ws://127.0.0.1:8090/maxcompress');
                return await new Promise((resolve, reject) => {
                    ws.onopen = () => ws.send('A'.repeat(10000));
                    ws.onmessage = (e) => resolve(e.data.length);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 15000);
                });
            }
        """)
        assert result == 10000, f"Expected 10000 bytes back, got {result}"
        browser.close()

    log_text = log_path.read_text()
    # Should have tunnel installed
    assert "ws_deflate: tunnel installed" in log_text, "Tunnel not installed"

    # But should NOT have compression (payload > max_compress_len=1024)
    for line in log_text.splitlines():
        if "ws_deflate: compressed" in line:
            pytest.fail(f"Found compression despite max_compress_len=1024: {line}")
