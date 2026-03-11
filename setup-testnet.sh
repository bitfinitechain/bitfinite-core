#!/bin/bash
# BFX Testnet Quick Start Script
# This script automates the initial testnet deployment

set -e

echo "🚀 BFX Testnet Deployment Script"
echo "=================================="
echo ""

# Configuration
BFX_DIR="."
TESTNET_DIR="$HOME/.bitfinite-testnet"
CONFIG_FILE="$TESTNET_DIR/bitfinite.conf"

# Step 1: Create testnet directory
echo "📁 Creating testnet directory..."
mkdir -p "$TESTNET_DIR"
echo "✅ Directory created: $TESTNET_DIR"
echo ""

# Step 2: Create configuration file
echo "⚙️  Creating configuration file..."
cat > "$CONFIG_FILE" << 'EOF'
# BFX Testnet Configuration
# Generated: 2026-01-30

# Network Configuration
testnet=1
listen=1
server=1

# RPC Configuration
rpcuser=bfxtestuser
rpcpassword=CHANGE_THIS_SECURE_PASSWORD_NOW
rpcallowip=127.0.0.1
rpcport=18332

# Network Ports
port=18333

# Logging (Enhanced for security monitoring)
debug=net
debug=mempoolrej
debug=blockencodings
printtoconsole=1
logips=1
logtimestamps=1

# Performance
dbcache=2048
maxmempool=300

# Connection Limits
maxconnections=125

# Security Monitoring
# These settings help validate CVE patches
EOF

echo "✅ Configuration file created: $CONFIG_FILE"
echo "⚠️  IMPORTANT: Edit $CONFIG_FILE and change the RPC password!"
echo ""

# Step 3: Display binary information
echo "📦 Binary Information:"
echo "   bitfinited:    $(ls -lh $BFX_DIR/build/src/bitfinited | awk '{print $5}')"
echo "   bitfinite-cli: $(ls -lh $BFX_DIR/build/src/bitfinite-cli | awk '{print $5}')"
echo "   bitfinite-tx:  $(ls -lh $BFX_DIR/build/src/bitfinite-tx | awk '{print $5}')"
echo ""

# Step 4: Provide next steps
echo "🎯 Next Steps:"
echo ""
echo "1. Edit the configuration file:"
echo "   nano $CONFIG_FILE"
echo "   (Change the rpcpassword!)"
echo ""
echo "2. Start the testnet node:"
echo "   cd $BFX_DIR"
echo "   ./build/src/bitfinited -datadir=$TESTNET_DIR -daemon"
echo ""
echo "3. Check node status:"
echo "   ./build/src/bitfinite-cli -datadir=$TESTNET_DIR getblockchaininfo"
echo ""
echo "4. Monitor logs:"
echo "   tail -f $TESTNET_DIR/testnet3/debug.log"
echo ""
echo "5. Stop the node (when needed):"
echo "   ./build/src/bitfinite-cli -datadir=$TESTNET_DIR stop"
echo ""

# Step 5: Create helper scripts
echo "📝 Creating helper scripts..."

# Start script
cat > "$TESTNET_DIR/start-testnet.sh" << EOF
#!/bin/bash
cd $BFX_DIR
./build/src/bitfinited -datadir=$TESTNET_DIR -daemon
echo "✅ BFX testnet node started"
echo "Monitor with: tail -f $TESTNET_DIR/testnet3/debug.log"
EOF
chmod +x "$TESTNET_DIR/start-testnet.sh"

# Stop script
cat > "$TESTNET_DIR/stop-testnet.sh" << EOF
#!/bin/bash
cd $BFX_DIR
./build/src/bitfinite-cli -datadir=$TESTNET_DIR stop
echo "✅ BFX testnet node stopped"
EOF
chmod +x "$TESTNET_DIR/stop-testnet.sh"

# Status script
cat > "$TESTNET_DIR/status-testnet.sh" << EOF
#!/bin/bash
cd $BFX_DIR
echo "🔍 BFX Testnet Status"
echo "===================="
echo ""
echo "Blockchain Info:"
./build/src/bitfinite-cli -datadir=$TESTNET_DIR getblockchaininfo 2>/dev/null || echo "Node not running"
echo ""
echo "Network Info:"
./build/src/bitfinite-cli -datadir=$TESTNET_DIR getnetworkinfo 2>/dev/null || echo "Node not running"
echo ""
echo "Peer Count:"
./build/src/bitfinite-cli -datadir=$TESTNET_DIR getconnectioncount 2>/dev/null || echo "Node not running"
EOF
chmod +x "$TESTNET_DIR/status-testnet.sh"

echo "✅ Helper scripts created:"
echo "   $TESTNET_DIR/start-testnet.sh"
echo "   $TESTNET_DIR/stop-testnet.sh"
echo "   $TESTNET_DIR/status-testnet.sh"
echo ""

echo "✅ Setup Complete!"
echo ""
echo "📚 Documentation:"
echo "   Deployment Guide: ~/.gemini/antigravity/brain/d5b66a84-a769-4ea9-af19-45d5804dbc94/testnet_deployment_guide.md"
echo "   Verification Report: ~/.gemini/antigravity/brain/d5b66a84-a769-4ea9-af19-45d5804dbc94/verification_report.md"
echo ""
echo "🎉 Ready to deploy to testnet!"
