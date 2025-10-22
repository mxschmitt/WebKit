FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-22.04

# Set environment variables for WebKit build
ENV WEBKIT_OUTPUTDIR=/workspace/WebKitBuild
ENV CMAKE_BUILD_TYPE=Release
ENV LANG=en_US.UTF-8

# Install WebKit build dependencies
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    git \
    curl \
    wget \
    pkg-config \
    # Development libraries
    libssl-dev \
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
    libgstreamer-plugins-base1.0-dev \
    # Tools
    nodejs \
    npm \
    ccache

# Install Python build dependencies
RUN python3 -m pip install --upgrade pip && \
    python3 -m pip install \
    setuptools \
    wheel \
    certifi

# Cleanup
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# Configure ccache for faster rebuilds
RUN mkdir -p /home/vscode/.ccache && \
    echo "max_size = 10G" > /home/vscode/.ccache/ccache.conf && \
    echo "compression = true" >> /home/vscode/.ccache/ccache.conf && \
    chown -R vscode:vscode /home/vscode/.ccache

# Create WebKitBuild directory with proper permissions
RUN mkdir -p /workspace/WebKitBuild && \
    chown -R vscode:vscode /workspace

WORKDIR /workspace
