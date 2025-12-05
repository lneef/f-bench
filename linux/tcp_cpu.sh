#!/bin/bash

# Number of iterations for the loop
ITERATIONS=3
# Duration in seconds for experiment
DURATION=3

ip="172.31.32.21"

# POSIX benchmark
POSIX_THREADS=(1 2 4 8 16 32 64 128)
POSIX_CONNS=(32 64 128)
for THREAD in "${POSIX_THREADS[@]}"
do
  for CONN in "${POSIX_CONNS[@]}"
  do
    if [ "$CONN" -lt "$THREAD" ]; then
      continue
    fi
    for ((i=1; i<=ITERATIONS; i++))
    do
      ./build/main -c $ip -P $CONN -t $DURATION -b 1 -x $THREAD -d 1 && \
      ./build/main -c $ip -P $CONN -t $DURATION -b 1 -x $THREAD -d 2 && \
      ./build/main -c $ip -P $CONN -t $DURATION -b 1 -x $THREAD -d 0
    done
  done
done

# Uring benchmark
URING_VERSIONS=(2 3)
URING_THREADS=(1 2 4 8)
URING_CONNS=(32 64 128)
# Loop over each connection value
for THREAD in "${URING_THREADS[@]}"
do
  for CONN in "${URING_CONNS[@]}"
  do
    for VERSION in "${URING_VERSIONS[@]}"
    do
      echo $URING
      # Loop to run iperf specified number of times
      for ((i=1; i<=ITERATIONS; i++))
      do
        echo $i && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 1 && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 2 && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 0
      done
    done
  done
done

URING_THREADS=(3 5 6)
URING_CONNS=(30 60 120)
# Loop over each connection value
for THREAD in "${URING_THREADS[@]}"
do
  for CONN in "${URING_CONNS[@]}"
  do
    for VERSION in "${URING_VERSIONS[@]}"
    do
      echo $URING
      # Loop to run iperf specified number of times
      for ((i=1; i<=ITERATIONS; i++))
      do
        echo $i && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 1 && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 2 && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 0
      done
    done
  done
done

URING_THREADS=(7)
URING_CONNS=(35 70 105)
# Loop over each connection value
for THREAD in "${URING_THREADS[@]}"
do
  for CONN in "${URING_CONNS[@]}"
  do
    for VERSION in "${URING_VERSIONS[@]}"
    do
      echo $URING
      # Loop to run iperf specified number of times
      for ((i=1; i<=ITERATIONS; i++))
      do
        echo $i && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 1 && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 2 && \
        ./build/main -c $ip -P $CONN -t $DURATION -b $VERSION -x $THREAD -d 0
      done
    done
  done
done
