#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input_file> <output_file>"
    exit 1
fi

INPUT=$1
OUTPUT=$2

ffmpeg -i "$INPUT" \
    -c:v h264 \
    -profile:v baseline \
    -level 3.0 \
    -preset ultrafast \
    -tune fastdecode \
    -vf "scale=1920:1080" \
    -pix_fmt yuv420p \
    -g 25 \
    -bf 0 \
    -threads 4 \
    -c:a copy \
    -movflags +faststart \
    -y \
    "$OUTPUT"