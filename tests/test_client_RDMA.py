#!/usr/bin/env python3

import sys
import subprocess
import time
import test_runner

# Set name of test to run
testPath = "./tests/client/rdmaclientmain"
# Call script to run the test
test_runner.runTestRDMA(testPath)
