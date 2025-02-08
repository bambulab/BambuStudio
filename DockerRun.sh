#!/bin/bash
set -x
# Just in case, here's some other things that might help:
#  Force the container's hostname to be the same as your workstation
#  -h $HOSTNAME \
#  If there's problems with the X display, try this
#  -v /tmp/.X11-unix:/tmp/.X11-unix \
#  or 
#  -v $HOME/.Xauthority:/root/.Xauthority \
#  You also need to run "xhost +" on your host system
# Bambu Studio also require the parent directory for the configuration directory to be present to start
#  to prevent your local machines's bambu studio config passed to docker container when you map your home directory, add:
# -v :SHOME/.config/BambuStudio
set -x
docker run \
  `# Use the hosts networking.  Printer wifi and also dbus communication` \
  --net=host \
  `# Some X installs will not have permissions to talk to sockets for shared memory` \
  --ipc host \
  `# Bind mount your home directory into the container for loading/saving files` \
  -v $HOME:$HOME \
  `# Pass some X Auth file to allow x11 to connect to your host x instance` \
  -v $HOME/.Xauthority:/tmp/.Xauthority \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -e XAUTHORITY=/tmp/.Xauthority \
  `# Pass the X display number to the container` \
  -e DISPLAY=$DISPLAY \
  `# It seems that libGL and dbus things need privileged mode` \
  --privileged=true \
  `# Attach tty for running bambu with command line things` \
  -ti \
  `# Remove container when it is finished` \
  --rm \
  `# Pass all parameters from this script to the bambu ENTRYPOINT binary` \
  bambustudio $* 
