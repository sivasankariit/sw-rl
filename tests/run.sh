#!/bin/bash

dir=`date +%b%d-%H:%M`
time=20
ns=0

mkdir -p $dir
for nrr in 512; do
for rl in newrl htb none; do
    python netperf.py --nrr $nrr --exptid rl-$rl -t $time \
        --ns $ns \
        --rl $rl

    mv rl-$rl.tar.gz $dir/

    pushd $dir;
    tar xf rl-$rl.tar.gz
    python ../plot.py --rr rl-$rl/* -o $rl.png --ymin 0.9
    popd;
done
done

echo Experiment results are in $dir
