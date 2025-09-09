import pytest
import subprocess
import socket
import time
from pathlib import Path
from playwright.sync_api import Page, expect, sync_playwright

# Import the server module under an alias
import server as viewer_server

# This is the base URL where our test server will be running.
BASE_URL = "https://127.0.0.1:8889"


# viewer/test_e2e.py (replace this fixture)

@pytest.fixture(scope="session")
def live_server_process():
    """
    A session-scoped fixture that starts the server in a detached tmux
    session and uses a robust polling loop to wait for it to be ready.
    """
    # Arrange: Command to start the server in a new tmux session.
    start_cmd = "tmux new -s test_server -d '. .venv/bin/activate && python3 server.py'"

    try:
        # Assuming tests are run from the 'viewer' directory.
        # The CWD needs to be the project root for paths in server.py to work.
        subprocess.run(start_cmd, shell=True, check=True, cwd="..")

        # Wait for the server to be ready with a robust polling loop.
        host, port = "127.0.0.1", 8889
        for _ in range(100):  # Poll for up to 10 seconds
            try:
                with socket.create_connection((host, port), timeout=0.1):
                    # If the connection succeeds, the server is up.
                    break
            except (ConnectionRefusedError, socket.timeout):
                # If refused or timeout, wait and retry.
                time.sleep(0.1)
        else:
            # If the loop finishes without breaking, the server failed to start.
            subprocess.run("tmux kill-session -t test_server", shell=True)
            pytest.fail(f"Server at {host}:{port} did not start within 10 seconds.")

        yield BASE_URL  # The tests run at this point

    finally:
        # Teardown: Cleanly kill the tmux session.
        kill_cmd = "tmux kill-session -t test_server"
        subprocess.run(kill_cmd, shell=True, stderr=subprocess.DEVNULL)


# --- E2E Test Cases ---
def test_page_load_and_sidebar(live_server_process):
    """Tests that the page loads and the sidebar populates with REAL articles."""
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(ignore_https_errors=True)
        page = context.new_page()
        page.goto(BASE_URL)

        expect(page).to_have_title("Article Viewer")

        # Assert that your actual articles appear in the sidebar
        sidebar = page.locator("#article-list")
        expect(sidebar.get_by_role("link", name="Basic Fastapi")).to_be_visible()
        expect(sidebar.get_by_role("link", name="Docker Dev Environment")).to_be_visible()
        expect(sidebar.get_by_role("link", name="Html Css Javascript Article Viewer")).to_be_visible()
        context.close()
        browser.close()


def test_article_click_and_render(live_server_process):
    """Tests clicking an article and verifying its content is rendered."""
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(ignore_https_errors=True)
        page = context.new_page()
        page.goto(BASE_URL)

        page.get_by_role("link", name="Docker Dev Environment").click()

        # Check for a unique heading from the real Docker article
        content_heading = page.locator("#content h2", has_text="A Primer on Isolation")
        expect(content_heading).to_be_visible()
        context.close()
        browser.close()


def test_code_block_highlighting(live_server_process):
    """Tests that highlight.js correctly processes code blocks."""
    with sync_playwright() as p:
        browser = p.chromium.launch()
        context = browser.new_context(ignore_https_errors=True)
        page = context.new_page()
        page.goto(BASE_URL)

        page.get_by_role("link", name="Basic Fastapi").click()

        # Assert that a span with a highlight.js class exists in a known code block
        highlighted_keyword = page.locator("span.hljs-keyword", has_text="from")
        expect(highlighted_keyword.first).to_be_visible()
        context.close()
        browser.close()

def test_sleep():
    """Just here to ensure that our server closes properly after the last test"""
    time.sleep(1)
