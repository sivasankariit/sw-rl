#!/bin/bash

dir=`date +%b%d-%H:%M`
time=30
ns=0

mkdir -p $dir
for ns in 512; do
for ssize in 64 128 256 512 1440 32000; do
for rl in newrl htb none; do
    exptid=rl-$rl-ssize-$ssize
    python netperf.py --ns $ns --nrr 0 \
        --exptid $exptid \
        -t $time \
        --rl $rl \
        --ssize $ssize

    mv $exptid.tar.gz $dir/

    pushd $dir;
    tar xf $exptid.tar.gz
    #python ../plot.py --rr $exptid/* -o $exptid.png --ymin 0.9
    popd;
done;
done
done

echo Experiment results are in $dir
