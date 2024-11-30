#!/bin/bash
set -x
# Just in case, here's some other things that might help:
#  Force the container's hostname to be the same as your workstation
#  -h $HOSTNAME \
#  If there's problems with the X display, try this
#  -v /tmp/.X11-unix:/tmp/.X11-unix \
#  or 
#  -v $HOME/.Xauthority:/root/.Xauthority
#  You also need to run "xhost +" on your host system
# Bambu Studio also require the parent directory for the configuration directory to be present to start
#  which means it is important to make sure user is passed to container correctly
#  if the following configuration does not work with error: "boost::filesystem::create_directory: No such file or directory"
#  try replacing -u line with 
#  -u $(id -u ${USER}):$(id -g ${USER}) \
#  and add 
#  -e HOME=/home/$USER \
docker run \
  `# Use the hosts networking.  Printer wifi and also dbus communication` \
  --net=host \
  `# Some X installs will not have permissions to talk to sockets for shared memory` \
  --ipc host \
  `# Run as your workstations username to keep permissions the same` \
  -u $USER \
  `# Bind mount your home directory into the container for loading/saving files` \
  -v $HOME:/home/$USER \
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
  
