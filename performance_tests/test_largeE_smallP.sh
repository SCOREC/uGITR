#!/bin/bash
# Bash script to run a series of ps_combo tests

# Medium-Sparsity Testing Script for AiMOS: large elm n, small ptcl n
for e in 100 200 500 1000 2000 5000 10000 20000 50000 100000
do
  for distribution in 1 2 3
  do 
    for percent in 50 # 10% and 50% should be very similar
    do 
      for struct in 0 1 2
      do
        mpirun -np 2 ./ps_combo160 $e $((e*1000)) $distribution -p $percent -n $struct
      done
    done
  done
done