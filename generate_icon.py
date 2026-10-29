#!/usr/bin/env python3
"""
generate_icon.py - Root wrapper for tools/generate_appicon.py
"""
import os
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
tool_script = os.path.join(script_dir, "tools", "generate_appicon.py")

if __name__ == "__main__":
    if os.path.exists(tool_script):
        # Forward arguments to main tool script
        cmd = [sys.executable, tool_script] + sys.argv[1:]
        import subprocess
        sys.exit(subprocess.call(cmd))
    else:
        print(f"Error: {tool_script} not found")
        sys.exit(1)
