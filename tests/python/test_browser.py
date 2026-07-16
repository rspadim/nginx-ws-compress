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


def test_websocket_roundtrip_via_browser(nginx_server):
    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        result = page.evaluate(
            """
            async () => {
                return new Promise((resolve, reject) => {
                    const ws = new WebSocket('ws://127.0.0.1:8090/ws');
                    ws.onopen = () => ws.send('browser hello');
                    ws.onmessage = (e) => resolve(e.data);
                    ws.onerror = (e) => reject(e.error?.message || 'ws error');
                    setTimeout(() => reject('timeout'), 10000);
                });
            }
        """
        )
        assert result == "browser hello"
        browser.close()
