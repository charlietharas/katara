#!/usr/bin/env python3
"""
Simple HTTP server for local development of the hand tracking WASM application.

This version does NOT require COOP/COEP headers since we use direct function calls
instead of SharedArrayBuffer. This means it works on GitHub Pages!

Usage: python3 server.py
Then open: http://localhost:8000/web/
"""

import os
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

# Change to the src directory to serve both web/ and js/
SRC_DIR = Path(__file__).parent / 'src'


class DevServerHandler(SimpleHTTPRequestHandler):
    """HTTP request handler for local development."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(SRC_DIR), **kwargs)

    def end_headers(self):
        # Add caching headers for WASM/JS files
        if self.path.endswith(('.wasm', '.js')):
            self.send_header('Cache-Control', 'no-cache')
        super().end_headers()

    def log_message(self, format, *args):
        """Custom logging format."""
        print(f"[{self.log_date_time_string()}] {format % args}")


def run_server(port=8000):
    """Start the HTTP server."""

    # Change to src directory
    os.chdir(SRC_DIR)

    server_address = ('', port)
    httpd = HTTPServer(server_address, DevServerHandler)

    print(f"==============================================")
    print(f"Hand Tracking WASM Development Server")
    print(f"==============================================")
    print(f"Server running at: http://localhost:{port}/web/")
    print(f"Serving directory: {SRC_DIR}")
    print(f"\nNote: This version uses direct function calls")
    print(f"instead of SharedArrayBuffer, so it works on")
    print(f"GitHub Pages and other static hosting services!")
    print(f"\nPress Ctrl+C to stop the server")
    print(f"==============================================\n")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n\nShutting down server...")
        httpd.shutdown()
        sys.exit(0)


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='HTTP server for hand tracking WASM app')
    parser.add_argument('--port', '-p', type=int, default=8000,
                        help='Port to listen on (default: 8000)')

    args = parser.parse_args()

    # Check if src directory exists
    if not SRC_DIR.exists():
        print(f"Error: Source directory not found: {SRC_DIR}")
        sys.exit(1)

    run_server(args.port)
