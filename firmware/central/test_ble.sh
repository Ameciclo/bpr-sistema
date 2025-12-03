#!/bin/bash

echo "🧪 BPR Central BLE Test Script"
echo "=============================="

# Check if PlatformIO is available
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO not found. Please install it first."
    exit 1
fi

echo "📋 Available test options:"
echo "1. Compile and upload Central (with BLE enabled)"
echo "2. Compile and upload Test Simulator"
echo "3. Monitor Central logs"
echo "4. Monitor Simulator logs"
echo "5. Clean build files"

read -p "Choose option (1-5): " option

case $option in
    1)
        echo "🔨 Compiling and uploading Central..."
        pio run -t upload
        echo "✅ Central uploaded. Use option 3 to monitor."
        ;;
    2)
        echo "🔨 Compiling and uploading Test Simulator..."
        pio run -e test_simulator -c test/platformio_test.ini -t upload
        echo "✅ Simulator uploaded. Use option 4 to monitor."
        ;;
    3)
        echo "📊 Monitoring Central logs..."
        pio device monitor
        ;;
    4)
        echo "📊 Monitoring Simulator logs..."
        pio device monitor -e test_simulator -c test/platformio_test.ini
        ;;
    5)
        echo "🧹 Cleaning build files..."
        pio run -t clean
        echo "✅ Clean completed."
        ;;
    *)
        echo "❌ Invalid option"
        ;;
esac