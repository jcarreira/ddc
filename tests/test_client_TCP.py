#!/usr/bin/env python3

import sys
import subprocess
import time
import test_runner

# Set name of test to run
testPath = "./tests/client/tcpclientmain"
# Call script to run the test
test_runner.runTestTCP(testPath)
