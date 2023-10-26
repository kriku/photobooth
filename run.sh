#!/usr/bin/env sh

./test &
pid=$!
echo $pid > run.pid
