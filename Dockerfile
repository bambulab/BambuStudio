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
    xvfb \
    eglexternalplatform-dev \
    extra-cmake-modules \
    file \
    git \
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
    libwebkit2gtk-4.1-dev \
    wget 

# Change your locale here if you want.  See the output
# of `locale -a` to pick the correct string formatting.
ENV LC_ALL=en_US.utf8
RUN locale-gen $LC_ALL

# Set this so that Bambu Studio doesn't complain about
# the CA cert path on every startup
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt

COPY ./ /BambuStudio

RUN chmod +x /BambuStudio/DockerEntrypoint.sh

WORKDIR /BambuStudio

# Ubuntu 24 Docker Image now come with default standard user "ubuntu"
# It might conflict with your mapped user, remove if user ubuntu exist
RUN if id "ubuntu" >/dev/null 2>&1; then userdel -r ubuntu; fi

# It's easier to run Bambu Studio as the same username,
# UID and GID as your workstation.  Since we bind mount
# your home directory into the container, it's handy
# to keep permissions the same.  Just in case, defaults
# are root.

# Set ARG values
# If user was passed from build it will create a user same
# as your workstation. Else it will use /root

# Setting ARG at build time is convienient for testing purposes
# otherwise the same commands will be executed at runtime

ARG USER=root
ARG UID=0
ARG GID=0
RUN if [ "$UID" != "0" ]; then \
      groupadd -g $GID $USER && \
      useradd -u $UID -g $GID -m -d /home/$USER $USER && \
      mkdir -p /home/$USER && \
      chown -R $UID:$GID /BambuStudio && \
      usermod -aG sudo $USER && \
      passwd -d "$USER"; \
    else \
      mkdir -p /root/.config; \
    fi

# Allow password-less sudo for ALL users
RUN echo "ALL ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/999-passwordless
RUN chmod 440 /etc/sudoers.d/999-passwordless

# Update System dependencies(Run before user switch)
RUN ./BuildLinux.sh -u

# Run as the mapped user (or root by default)
USER $USER


# These can run together, but we run them seperate for podman caching
# Build dependencies in ./deps
RUN ./BuildLinux.sh -d

# Build slic3r
RUN ./BuildLinux.sh -s

# Build AppImage
ENV container=podman
RUN ./BuildLinux.sh -i


# Use bash as the shell
SHELL ["/bin/bash", "-l", "-c"]

# Point FFMPEG Library search to the binary built upon BambuStudio build time
ENV LD_LIBRARY_PATH=/BambuStudio/build/package/bin

# Using an entrypoint instead of CMD because the binary
# accepts several command line arguments.
# entrypoint script will pass all arguments to bambu-studio
# after the script finishes

#ENTRYPOINT ["/BambuStudio/build/package/bin/bambu-studio"]
ENTRYPOINT ["/BambuStudio/DockerEntrypoint.sh"]
