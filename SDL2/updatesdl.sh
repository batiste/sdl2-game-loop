#!/bin/bash

prefixDir=`pwd`/sdl2-x86

# ------------------------------ Functions Start ------------------------------
function cloneSdlRepo {
   (hg clone http://hg.libsdl.org/$1 && cd SDL && hg co default && hg pull -u)
}

function pullAndBuildSdlRepo {
    echo "
------------------------------------------------------------
                 Building $1
------------------------------------------------------------"
    sleep 1
    echo "Updating $1 from repo"
    cd $1
    hg co default
    hg pull -u
    ./autogen.sh
    rm -rf build
    mkdir build
    cd build
    ../configure --prefix=$prefixDir
    make
    make install
    cd ../..
    echo "
------------------------------------------------------------
                 Finished Building $1
------------------------------------------------------------"
}
# ------------------------------ Functions End ------------------------------



# ------------------------------ Script Start ------------------------------

echo "Performing update of sdl2
"
echo -n "Removing current library..."
rm -rf sdl2-x86
mkdir sdl2-x86
echo " done"

if [ ! -e SDL ] # Just simply check for SDL since all other depend on it
then
    echo "SDL Repo not found. Checking out all repositories"
    rm -rf SDL SDL_image SDL_mixer SDL_net SDL_ttf
    cloneSdlRepo "SDL" &
    cloneSdlRepo "SDL_image" &
    cloneSdlRepo "SDL_mixer" &
    cloneSdlRepo "SDL_ttf" &
    cloneSdlRepo "SDL_net"
    echo "
# ------------------------------------------------------------
#          Waiting for Repo Checkout
# ------------------------------------------------------------"
    wait $(jobs -p)
    echo "
------------------------------------------------------------
         Finished checking out repositories
------------------------------------------------------------"
    sleep 1
fi

####  SDL BUILD
pullAndBuildSdlRepo "SDL"
export SDL_CONFIG=$prefixDir/bin/sdl2-config
if [ ! -f $SDL_CONFIG ]
then
    echo "Did not find sdl2-config"
    exit
fi

####  Build the rest in parallel
pullAndBuildSdlRepo "SDL_image"  &
pullAndBuildSdlRepo "SDL_mixer" &
pullAndBuildSdlRepo "SDL_ttf" &
pullAndBuildSdlRepo "SDL_net"


wait $(jobs -p)
echo "
------------------------------------------------------------
         Finished Rebuild All Repositories!
------------------------------------------------------------"
