#!/usr/bin/env sh

if test -f run.pid; then
    pid=$(cat run.pid)
    rm run.pid
    kill $pid
fi
