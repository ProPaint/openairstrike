#!/usr/bin/env python3
"""The receiver of the 3DS log push (tools/3ds_receive.py --self-test): a stream in the framing of
apps/3ds/source/soak_report.h (whose bytes apps/tests/soak_report_test.cpp pins) through a local
socket, a refused file name, a stream cut short. No game data; `--game` is accepted and ignored
(tools/ci.sh passes it to every test here)."""
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.exit(subprocess.run([sys.executable, os.path.join(here, "..", "3ds_receive.py"), "--self-test"]).returncode)
