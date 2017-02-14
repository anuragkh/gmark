#!/bin/bash

folder=${1:-"../demo/social/"}

cd ../../src
./test -c ../use-cases/social-network.xml -g "$folder"social-graph.txt -w "$folder"social-workload.xml -r "$folder"
mkdir -p "$folder"raw
awk '{ print $1 ",p" $2 "," $3 "," systime() }' "$folder"social-graph.txt > "$folder"raw/edges.txt
max=`awk 'BEGIN { max = 0 } { if ($1 > max) { max = $1 } if ($3 > max) { max = $3 } } END { print max }' "$folder"social-graph.txt`
seq 0 $max > "$folder"raw/nodes.txt

cd querytranslate
./test -w ../"$folder"social-workload.xml -o ../"$folder"social-translated

cd ../queryinterface
./test -w ../"$folder"social-workload.xml -t ../"$folder"social-translated -o ../"$folder"social-interface
