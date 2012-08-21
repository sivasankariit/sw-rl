#!/bin/bash

dir="$1"

if [ -z "$dir" ]; then
    echo "usage: $(basename $0) expt-dir"
    exit 1;
fi

pushd $dir;
for nrr in 512; do
for rl in newrl htb none; do
    python ../plot.py --rr rl-$rl/* -o $rl.png --ymin 0.9
done
done
popd;
