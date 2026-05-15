#!/bin/sh
set -e

# Multi-account mode: ACCOUNTS="user1:pw1,user2:pw2,user3:pw3"
# Account N is served on ports decrypt=10020+N, m3u8=20020+N, account=30020+N
# Each account gets its own state dir under /data/data/com.apple.android.music/files/account_N
if [ -n "${ACCOUNTS:-}" ]; then
  exec ./wrapper \
    -L "$ACCOUNTS" \
    -F \
    -H 0.0.0.0 \
    "$@"
fi

# Single-account mode (backward compat): USERNAME + PASSWORD env vars.
# Skips -L when a cached login database is present.
TOKEN_DB_PATH="/app/rootfs/data/data/com.apple.android.music/files/mpl_db/kvs.sqlitedb"

if [ ! -f "$TOKEN_DB_PATH" ]; then
  echo "Login required: account database not found."
  if [ -z "${USERNAME:-}" ] || [ -z "${PASSWORD:-}" ]; then
    echo "Error: set ACCOUNTS=\"user1:pw1,user2:pw2,...\" for multi-account," >&2
    echo "       or USERNAME and PASSWORD env vars for single-account." >&2
    exit 1
  fi
  exec ./wrapper \
    -L "${USERNAME}:${PASSWORD}" \
    -F \
    -H 0.0.0.0 \
    "$@"
else
  exec ./wrapper \
    -H 0.0.0.0 \
    "$@"
fi
