#!/bin/sh
# GitHub runners: retry transient mirror failures, with a limit on silent connections.
attempt=1
while :; do
    if sudo apt-get -o Acquire::Retries=5 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 "$@"; then
        exit 0
    else
        status=$?
    fi
    if [ "$attempt" -ge 3 ]; then
        exit "$status"
    fi
    echo "apt-get: attempt $attempt failed ($status); trying again in 60 seconds" >&2
    sleep 60
    attempt=$((attempt + 1))
done
