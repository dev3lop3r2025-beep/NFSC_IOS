#!/usr/bin/env python3
"""
tools/prepare_sdk.py - Root wrapper for tools/ios/prepare_sdk.py
"""
import os
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
tool_script = os.path.join(script_dir, "ios", "prepare_sdk.py")

if __name__ == "__main__":
    if os.path.exists(tool_script):
        import subprocess
        cmd = [sys.executable, tool_script] + sys.argv[1:]
        sys.exit(subprocess.call(cmd))
    else:
        print(f"Error: {tool_script} not found")
        sys.exit(1)
