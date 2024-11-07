FROM docker.io/ubuntu:24.10

# Disable interactive package configuration
RUN apt-get update && \
    echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections

# Add a deb-src
RUN echo deb-src http://archive.ubuntu.com/ubuntu \
    $(cat /etc/*release | grep VERSION_CODENAME | cut -d= -f2) main universe>> /etc/apt/sources.list 

RUN apt-get update && apt-get install  -y \
    autoconf \
    build-essential \
    cmake \
    curl \
    bc \
    xvfb \
    eglexternalplatform-dev \
    extra-cmake-modules \
    file \
    git \
    ffmpeg \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-libav \
    libcairo2-dev \
    libcurl4-openssl-dev \
    libdbus-1-dev \
    libglew-dev \ 
    libglu1-mesa-dev \
    libglu1-mesa-dev \
    libgstreamer1.0-dev \
    libgstreamerd-3-dev \ 
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    libgtk-3-dev \
    libgtk-3-dev \
    libosmesa6-dev \
    libsecret-1-dev \
    libsoup2.4-dev \
    libssl3 \
    libssl-dev \
    libudev-dev \
    libwayland-dev \
    libxkbcommon-dev \
    locales \
    locales-all \
    m4 \
    pkgconf \
    sudo \
    wayland-protocols \
    bash \
    wget 

#Temporary fix for 24.10 dependency
RUN echo 'deb http://gb.archive.ubuntu.com/ubuntu jammy main' >> /etc/apt/sources.list

RUN apt-get update && apt-get install -y libwebkit2gtk-4.0-dev

# Change your locale here if you want.  See the output
# of `locale -a` to pick the correct string formatting.
ENV LC_ALL=en_US.utf8
RUN locale-gen $LC_ALL

# Set this so that Bambu Studio doesn't complain about
# the CA cert path on every startup
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt

COPY ./ BambuStudio

WORKDIR BambuStudio

# These can run together, but we run them seperate for podman caching
# Update System dependencies
RUN ./BuildLinux.sh -u

# Build dependencies in ./deps
RUN ./BuildLinux.sh -d

# Build slic3r
RUN ./BuildLinux.sh -s

# Build AppImage
ENV container podman
RUN ./BuildLinux.sh -i

# It's easier to run Bambu Studio as the same username,
# UID and GID as your workstation.  Since we bind mount
# your home directory into the container, it's handy
# to keep permissions the same.  Just in case, defaults
# are root.
# Use bash as the shell
SHELL ["/bin/bash", "-l", "-c"]

# Set ARG values
ARG USER=root
ARG UID=0
ARG GID=0

# Run the commands with proper bash syntax
RUN if [ "$UID" != "0" ]; then \
      groupadd -f -g $GID $USER && \
      useradd -u $UID -g $GID $USER; \
    fi

#RUN chmod +x entrypoint.sh

# Using an entrypoint instead of CMD because the binary
# accepts several command line arguments.
ENTRYPOINT ["/BambuStudio/build/package/bin/bambu-studio"]