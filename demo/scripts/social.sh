#!/bin/bash

folder=${1:-"../demo/social/"}

cd ../../src/querytranslate

for num_nodes in 500000 1000000 2000000 4000000 8000000; do
  data_dir="${folder}${num_nodes}/"
  mkdir -p $data_dir

  sed -i "s/<nodes>.*<\\/nodes>/<nodes>$num_nodes<\\/nodes>/g" ../use-cases/social-network.xml
  mkdir -p ../"$data_dir"social-translated
  ./test -w ../"$folder"social-workload.xml -o ../"$data_dir"social-translated
done
