#!/bin/bash
set -e

echo "Building 01-rotating-cube..."

mkdir -p build
cd build

emcmake cmake ..
emmake make

echo ""
echo "Build complete!"
echo "Output: build/d3d9-rotating-cube.html"
echo ""
echo "To test, run a local server:"
echo "  cd build && python3 -m http.server 8000"
echo "Then open: http://localhost:8000/d3d9-rotating-cube.html"
