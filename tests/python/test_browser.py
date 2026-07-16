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


def test_varying_payload_sizes(nginx_server):
    """
    Send messages of increasing sizes: 1, 10, 100, 1K, 10K, 100K, 500K bytes.
    Verify each echo matches exactly.
    No buffer tuning needed — the module handles it transparently.
    """
    from playwright.sync_api import sync_playwright

    sizes = [1, 10, 100, 1000, 10_000, 100_000, 500_000]

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()

        js_sends = "const payloads = [" + ",".join(
            f'{{size:{s}, data:"A".repeat({s})}}' for s in sizes
        ) + "];"

        result = page.evaluate(f"""
            async () => {{
                {js_sends}
                const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                const results = [];
                let idx = 0;

                await new Promise((resolve, reject) => {{
                    ws.onopen = () => ws.send(payloads[0].data);
                    ws.onmessage = (e) => {{
                        results.push({{
                            size: payloads[idx].size,
                            len: e.data.length,
                            ok: e.data === payloads[idx].data
                        }});
                        idx++;
                        if (idx < payloads.length) {{
                            ws.send(payloads[idx].data);
                        }} else {{
                            resolve(results);
                        }}
                    }};
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 30000);
                }});
                return JSON.stringify(results);
            }}
        """)

        import json
        parsed = json.loads(result)
        for item in parsed:
            assert item["ok"], (
                f"FAIL at size={item['size']}: "
                f"expected len={item['size']}, got len={item['len']}"
            )
        print(f"\n  Payload sizes verified: {[s['size'] for s in parsed]}")
        browser.close()


def test_corner_case_payloads(nginx_server):
    """
    Corner cases: empty, 1B, and boundary tests to ensure the extension
    never breaks WebSocket regardless of message size.
    """
    from playwright.sync_api import sync_playwright

    # sizes that stress boundaries: 0, 1, 125 (max 7-bit), 126-127 (16-bit boundary),
    # 65535-65536 (16/64-bit boundary), then large
    sizes = [0, 1, 125, 126, 127, 65535, 65536, 100_000, 500_000]

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()

        # Build JS payloads (empty string needs special handling)
        items = []
        for s in sizes:
            if s == 0:
                items.append('{size:0, data:""}')
            else:
                items.append(f'{{size:{s}, data:"A".repeat({s})}}')

        js_sends = "const payloads = [" + ",".join(items) + "];"

        result = page.evaluate(f"""
            async () => {{
                {js_sends}
                const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                const results = [];
                let idx = 0;

                await new Promise((resolve, reject) => {{
                    ws.onopen = () => ws.send(payloads[0].data);
                    ws.onmessage = (e) => {{
                        const expected = payloads[idx].data;
                        results.push({{
                            size: payloads[idx].size,
                            len: e.data.length,
                            ok: e.data === expected,
                            sentLen: expected.length
                        }});
                        idx++;
                        if (idx < payloads.length) {{
                            ws.send(payloads[idx].data);
                        }} else {{
                            resolve(results);
                        }}
                    }};
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 30000);
                }});
                return JSON.stringify(results);
            }}
        """)

        import json
        parsed = json.loads(result)
        for item in parsed:
            assert item["ok"], (
                f"FAIL at size={item['size']}: "
                f"sent {item['sentLen']}B, got {item['len']}B"
            )
        print(f"\n  Corner cases verified: sizes={[s['size'] for s in parsed]}")
        browser.close()


