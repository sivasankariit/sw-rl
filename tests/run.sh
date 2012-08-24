#!/bin/bash

dir=`date +%b%d-%H:%M`
time=20
ns=0

mkdir -p $dir
for nrr in 512 1024; do
for rrsize in 1 10000 1000000; do
for rl in newrl htb none; do
    exptid=rl-$rl-rrsize-$rrsize
    python netperf.py --nrr $nrr \
        --exptid $exptid \
        -t $time \
        --ns $ns \
        --rl $rl \
        --rrsize $rrsize

    mv $exptid.tar.gz $dir/

    pushd $dir;
    tar xf $exptid.tar.gz
    python ../plot.py --rr $exptid/* -o $exptid.png --ymin 0.9
    popd;
done;
done
done

echo Experiment results are in $dir
