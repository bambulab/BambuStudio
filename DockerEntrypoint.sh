#!/bin/bash

# Entrypoint script to create an out-of-the-box experience for BambuStudio.
# Perform some initial setup if none was done previously.
# It is not necessary if you know what you are doing. Feel free to go
# to the Dockerfile and switch the entrypoint to the BambuStudio binary.

# Check if the current effective user is root

if [ "$EUID" -eq 0 ]; then
    echo "No User specified at build time."
    if [ -z "$RUN_USER" ] || [ -z "$RUN_UID" ] || [ -z "$RUN_GID" ] || [ "$RUN_UID" -eq 0 ]; then
        echo "At least one of RUN_USER, RUN_UID, or RUN_GID is unset. Or 'root' was requested."
        echo "Running as root"
        
        if [ "$HOME" != "/root" ]; then
            if [ ! -d "/root" ]; then
                mkdir /root
                chown root:root /root
                chmod 700 /root
            fi
        fi

        export HOME="/root"
        EXEC_USER="root"
    else
        echo "Setting up a new user"

        # Check if there is a already a valid user entry for the passed UID, if not create one
        if [ -z "$(getent passwd "$RUN_UID" | cut -d: -f1)" ]; then
            #GID=$(id -g)
            echo "User specified at runtime. Performing setup."
            groupadd -g "$RUN_GID" "$RUN_USER"
            useradd -u "$RUN_UID" -g "$RUN_GID" -d "/home/$RUN_USER" "$RUN_USER"
            usermod -aG sudo "$RUN_USER"
            passwd -d "$RUN_USER"

            #This will take forever to run, so we will just chown the build folder which contains the binaries
            #chown -R "$RUN_UID":"$RUN_GID" /BambuStudio
            chown "$RUN_UID":"$RUN_GID" /BambuStudio
            chown -R "$RUN_UID":"$RUN_GID" /BambuStudio/build


            export HOME="/home/$RUN_USER"
            EXEC_USER="$RUN_USER"
        fi
    fi
else
    echo "User specified at build time."
    CURRENT_USER=$(id -un)
    if [ -n "$RUN_USER" ] && [ -n "$RUN_UID" ] && [ -n "$RUN_GID" ] && [ "$RUN_UID" -ne "$EUID" ]; then
        echo "New User config passed at Runtime. Setting up."
        if [ -z "$(getent passwd "$RUN_UID" | cut -d: -f1)" ]; then
            sudo groupadd -g "$RUN_UID" "$RUN_USER"
            sudo useradd -u "$RUN_UID" -g "$RUN_GID" -d "/home/$RUN_USER" "$RUN_USER"
            sudo usermod -aG sudo "$RUN_USER"
            passwd -d "$RUN_USER"

            #sudo chown -R "$RUN_UID":"$RUN_GID" /BambuStudio
            chown "$RUN_UID":"$RUN_GID" /BambuStudio
            chown -R "$RUN_UID":"$RUN_GID" /BambuStudio/build

            export HOME="/home/$RUN_USER"
            EXEC_USER="$RUN_USER"
        fi
    else
        echo "Using Build time user."
        EXEC_USER="$CURRENT_USER"
        #It should've been set in Dockerfile, but just in case, uncomment this it there is problem
        #export HOME="/home/$USER"
    fi
fi

# make sure ~/.config folder exists so Bambu Studio will start
if [ ! -d "$HOME/.config" ]; then
    mkdir -p "$HOME/.config"
fi

# Using su $USER -c will retain all the important ENV args when Bamboo Studio starts in a different shell
# Continue with Bambu Studio using correct user, passing all arguments
exec su "$EXEC_USER" -c "/BambuStudio/build/package/bin/bambu-studio $*"
