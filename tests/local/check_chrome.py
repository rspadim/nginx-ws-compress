#!/usr/bin/env python3
"""Check if Playwright Chromium is available."""
from playwright.sync_api import sync_playwright

try:
    with sync_playwright() as p:
        b = p.chromium.launch(headless=True)
        print("CHROMIUM OK")
        b.close()
except Exception as e:
    print(f"ERROR: {e}")
