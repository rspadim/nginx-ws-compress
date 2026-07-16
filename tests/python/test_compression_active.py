"""
Smoke tests for compression negotiation.
These verify that Chrome can connect and exchange messages through
the module with ws_deflate enabled. The actual compression ratio
varies by platform — what matters is that the extension doesn't break
WebSocket functionality.
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


def test_compression_smoke(nginx_server):
    """Chrome connects with permessage-deflate, sends and receives a message."""
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        result = page.evaluate("""
            async () => {
                const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                return await new Promise((resolve, reject) => {
                    ws.onopen = () => ws.send('compression-smoke');
                    ws.onmessage = (e) => resolve(e.data);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 15000);
                });
            }
        """)
        assert result == "compression-smoke", f"Got: {result}"
        browser.close()


def test_max_compress_len_smoke(nginx_server):
    """Location with ws_deflate_max_compress_len works correctly."""
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        result = page.evaluate("""
            async () => {
                const ws = new WebSocket('ws://127.0.0.1:8090/maxcompress');
                return await new Promise((resolve, reject) => {
                    ws.onopen = () => ws.send('large-payload-test');
                    ws.onmessage = (e) => resolve(e.data);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 15000);
                });
            }
        """)
        assert result == "large-payload-test"
        browser.close()
