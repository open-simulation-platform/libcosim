#!/bin/bash
#
# This script removes cse generated files from the temp directory,
# which for some reason has not been disposed due to a program crash etc.
#
# Works on Windows and Linux
#

cd /tmp

# cse-core and cse-core4j
rm -rf cse_*

# FMI4j (fmu-proxy)
rm -rf fmi4j_*

# Other known FMI related files

# sintef vessel model FMU
rm -rf vesselFmu_*

# JavaFMI & JNA
rm -rf fmu_*
rm -rf org.javafmi*
rm -rf native-platform*
