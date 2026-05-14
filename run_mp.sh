pkill -f arbiter 2>/dev/null
pkill -f hip 2>/dev/null
pkill -f asp 2>/dev/null
rm -f /dev/shm/chrono_rift* 2>/dev/null
sleep 0.3

cd "$(dirname "$0")"

if [ ! -x ./bin/arbiter ] || [ ! -x ./bin/hip ] || [ ! -x ./bin/asp ]; then
    echo "Binaries missing. Run 'make' first."
    exit 1
fi

echo "Launching multiplayer Chrono Rift..."
echo "  - HIP A controls player slots 0, 1"
echo "  - HIP B controls player slots 2, 3"
echo "  - 4 players total vs shared NPCs"
echo

./bin/arbiter --mp --players=4 --enemies=5 --tick-ms=100 --kills=10 &
ARB_PID=$!
sleep 0.5

# HIP A first player station
./bin/hip --mp-slots=0,1 &
HIP_A_PID=$!
sleep 0.3

# HIP B second player station
./bin/hip --mp-slots=2,3 &
HIP_B_PID=$!
sleep 0.3

# Strategic process for NPCs
./bin/asp &
ASP_PID=$!

echo "PIDs: arbiter=$ARB_PID  hipA=$HIP_A_PID  hipB=$HIP_B_PID  asp=$ASP_PID"
echo "Waiting for all processes to exit..."

# When the user closes a HIP window, that HIP sends SIGTERM to the arbiter,
# which then cleans up and exits. ASP exits when it sees GS_QUIT.
wait $ARB_PID
echo "Arbiter exited. Cleaning up any stragglers..."
kill $HIP_A_PID $HIP_B_PID $ASP_PID 2>/dev/null
wait 2>/dev/null
echo "All processes done."
