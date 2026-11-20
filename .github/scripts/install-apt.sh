#!/usr/bin/env bash
set -euo pipefail

# Hosted runners may spend many minutes on a failed regional HTTP mirror.
# Keep Ubuntu's signed sources, with independent and official HTTPS mirrors.
mirrors=${TYPIST_APT_MIRRORS_FILE:-/etc/apt/apt-mirrors.txt}
if [[ -f "$mirrors" ]]; then
  printf '%s\n' \
    $'https://mirror.math.princeton.edu/pub/ubuntu/\tpriority:1' \
    $'https://archive.ubuntu.com/ubuntu/\tpriority:2' \
    $'https://security.ubuntu.com/ubuntu/\tpriority:3' | sudo tee "$mirrors" > /dev/null
fi

options=(-o Acquire::Retries=1 -o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15)
timeout 5m sudo apt-get "${options[@]}" update
timeout 10m sudo apt-get "${options[@]}" install -y -- "$@"
