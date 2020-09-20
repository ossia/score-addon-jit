#!/bin/bash

mkdir $HEADERS/qt
rsync -a $OSSIA_SDK/qt5-static/include/ $HEADERS/qt/
rsync -a $OSSIA_SDK/llvm/include/ $HEADERS/
rsync -a $OSSIA_SDK/ffmpeg/include/ $HEADERS/
rsync -a $OSSIA_SDK/fftw/include/ $HEADERS/
rsync -a $OSSIA_SDK/portaudio/include/ $HEADERS/

