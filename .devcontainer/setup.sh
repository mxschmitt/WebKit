#!/bin/bash
set -e

echo "🔧 WebKit Development Environment Setup"
echo "======================================="

# Update package lists
echo "📦 Updating package manager..."
apt-get update
apt-get upgrade -y

# Install essential build tools
echo "🛠️  Installing build tools..."
apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    git \
    curl \
    wget \
    pkg-config \
    libssl-dev \
    libglib2.0-dev \
    libgtk-3-dev

# Install WebKit-specific dependencies for GTK port
echo "🌐 Installing WebKit GTK port dependencies..."
apt-get install -y \
    libglib2.0-dev \
    libgtk-3-dev \
    libwayland-dev \
    libxkbcommon-dev \
    libepoxy-dev \
    libwebp-dev \
    libjpeg-dev \
    libpng-dev \
    libsqlite3-dev \
    libxslt1-dev \
    libicu-dev \
    libharfbuzz-dev \
    libfreetype6-dev \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev

# Install Python dependencies
echo "🐍 Installing Python dependencies..."
python3 -m pip install --upgrade pip
python3 -m pip install \
    setuptools \
    wheel \
    certifi

# Install Node.js (useful for some build scripts)
echo "📦 Installing Node.js..."
apt-get install -y nodejs npm

# Create build directory
echo "📁 Creating WebKitBuild directory..."
mkdir -p WebKitBuild

# Configure git
echo "🔐 Configuring git..."
git config --global pull.rebase true || true

echo ""
echo "✅ Setup complete!"
echo ""
echo "🚀 Quick start commands:"
echo "   Release build:  Tools/Scripts/build-webkit --release"
echo "   Debug build:    Tools/Scripts/build-webkit --debug"
echo "   Run tests:      Tools/Scripts/run-webkit-tests"
echo "   Run JSC tests:  Tools/Scripts/run-javascriptcore-tests"
echo ""
