"""
Browser-level tests using Playwright + Chrome.
Verifies WebSocket message roundtrip through nginx with compression.
"""
import pytest


def _chromium_available():
    try:
        from playwright.sync_api import sync_playwright
        with sync_playwright() as p:
            p.chromium.launch(headless=True).close()
        return True
    except Exception:
        return False


pytestmark = pytest.mark.skipif(
    not _chromium_available(), reason="Playwright Chromium not available"
)


def test_single_message_roundtrip(nginx_server):
    """Open WebSocket, send a message, verify echo."""
    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        result = page.evaluate("""
            async () => {
                return new Promise((resolve, reject) => {
                    const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                    ws.onopen = () => ws.send('ping');
                    ws.onmessage = (e) => resolve(e.data);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 10000);
                });
            }
        """)
        assert result == "ping", f"Expected 'ping', got '{result}'"
        browser.close()


def test_multiple_ping_pong(nginx_server):
    """Send 10 sequential messages, verify each response."""
    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        result = page.evaluate("""
            async () => {
                const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                const results = [];
                await new Promise((resolve, reject) => {
                    let i = 0;
                    ws.onopen = () => {
                        for (let j = 0; j < 10; j++) {
                            ws.send('msg-' + j);
                        }
                    };
                    ws.onmessage = (e) => {
                        results.push(e.data);
                        i++;
                        if (i === 10) resolve(results);
                    };
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 10000);
                });
                return results.join(',');
            }
        """)
        expected = ",".join(f"msg-{i}" for i in range(10))
        assert result == expected, f"Expected '{expected}', got '{result}'"
        browser.close()


def test_large_payload_ping_pong(nginx_server):
    """Send a 10KB payload, verify echo."""
    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        payload = "X" * 10_000
        result = page.evaluate(f"""
            async () => {{
                const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                return await new Promise((resolve, reject) => {{
                    ws.onopen = () => ws.send('{payload}');
                    ws.onmessage = (e) => resolve(e.data.length);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 15000);
                }});
            }}
        """)
        assert result == 10_000, f"Expected 10000 bytes, got {result}"
        browser.close()
