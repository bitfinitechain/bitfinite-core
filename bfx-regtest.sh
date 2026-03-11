#!/bin/bash
# BFX Regtest Node Manager
# Quick helper script for managing BFX regtest node

set -e

BFX_DIR="."
DATA_DIR="$HOME/.bitfinite-regtest"
CLI="$BFX_DIR/build/src/bitfinite-cli -regtest -datadir=$DATA_DIR"
DAEMON="$BFX_DIR/build/src/bitfinited -regtest -datadir=$DATA_DIR"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

function print_status() {
    echo -e "${GREEN}✅ $1${NC}"
}

function print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

function print_error() {
    echo -e "${RED}❌ $1${NC}"
}

function start_node() {
    echo "Starting BFX regtest node..."
    if pgrep -f "bitfinited.*regtest" > /dev/null; then
        print_warning "Node is already running"
        return 0
    fi
    
    $DAEMON -daemon
    sleep 3
    
    if pgrep -f "bitfinited.*regtest" > /dev/null; then
        print_status "Node started successfully"
        $CLI getblockchaininfo | jq -r '"Chain: \(.chain), Blocks: \(.blocks)"'
    else
        print_error "Failed to start node"
        return 1
    fi
}

function stop_node() {
    echo "Stopping BFX regtest node..."
    if ! pgrep -f "bitfinited.*regtest" > /dev/null; then
        print_warning "Node is not running"
        return 0
    fi
    
    $CLI stop 2>/dev/null || killall -9 bitfinited 2>/dev/null
    sleep 2
    
    if ! pgrep -f "bitfinited.*regtest" > /dev/null; then
        print_status "Node stopped successfully"
    else
        print_error "Failed to stop node"
        return 1
    fi
}

function status() {
    echo "=== BFX Regtest Node Status ==="
    
    if pgrep -f "bitfinited.*regtest" > /dev/null; then
        print_status "Node is RUNNING"
        echo ""
        echo "Blockchain Info:"
        $CLI getblockchaininfo | jq -r '
            "  Chain: \(.chain)",
            "  Blocks: \(.blocks)",
            "  Best Block: \(.bestblockhash)",
            "  Difficulty: \(.difficulty)",
            "  Verification: \(.verificationprogress * 100 | floor)%"
        '
        echo ""
        echo "Network Info:"
        $CLI getnetworkinfo | jq -r '
            "  Version: \(.subversion)",
            "  Protocol: \(.protocolversion)",
            "  Connections: \(.connections)"
        '
        echo ""
        echo "Memory Info:"
        $CLI getmemoryinfo | jq -r '.locked | "  Used: \(.used) bytes, Free: \(.free) bytes"'
    else
        print_warning "Node is NOT running"
    fi
}

function generate_blocks() {
    local count=${1:-10}
    echo "Generating $count blocks..."
    
    if ! pgrep -f "bitfinited.*regtest" > /dev/null; then
        print_error "Node is not running. Start it first with: $0 start"
        return 1
    fi
    
    $CLI generate $count | head -3
    print_status "Generated $count blocks"
    $CLI getblockcount
}

function logs() {
    local lines=${1:-50}
    echo "=== Last $lines lines of debug.log ==="
    tail -n $lines "$DATA_DIR/regtest/debug.log"
}

function watch_logs() {
    echo "=== Watching debug.log (Ctrl+C to stop) ==="
    tail -f "$DATA_DIR/regtest/debug.log"
}

function clean() {
    echo "Cleaning regtest data directory..."
    read -p "This will delete all blockchain data. Are you sure? (yes/no): " confirm
    
    if [ "$confirm" != "yes" ]; then
        print_warning "Cancelled"
        return 0
    fi
    
    stop_node
    rm -rf "$DATA_DIR/regtest"
    print_status "Regtest data cleaned"
}

function help() {
    cat << EOF
BFX Regtest Node Manager

Usage: $0 <command> [options]

Commands:
    start           Start the regtest node
    stop            Stop the regtest node
    restart         Restart the regtest node
    status          Show node status and info
    generate [N]    Generate N blocks (default: 10)
    logs [N]        Show last N lines of debug.log (default: 50)
    watch           Watch debug.log in real-time
    clean           Clean regtest data directory
    help            Show this help message

Examples:
    $0 start
    $0 generate 100
    $0 status
    $0 logs 100
    $0 watch

EOF
}

# Main command handler
case "${1:-help}" in
    start)
        start_node
        ;;
    stop)
        stop_node
        ;;
    restart)
        stop_node
        sleep 2
        start_node
        ;;
    status)
        status
        ;;
    generate)
        generate_blocks "${2:-10}"
        ;;
    logs)
        logs "${2:-50}"
        ;;
    watch)
        watch_logs
        ;;
    clean)
        clean
        ;;
    help|--help|-h)
        help
        ;;
    *)
        print_error "Unknown command: $1"
        echo ""
        help
        exit 1
        ;;
esac
