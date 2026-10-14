#!/usr/bin/env bash
# Entrypoint for viam-server. Bundled Viam shared libraries live next to this script.

set -euo pipefail

cd "$(dirname "$0")"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH-}:$PWD"
exec ./litratech-lidar "$@"
