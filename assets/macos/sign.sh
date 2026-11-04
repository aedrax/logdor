#!/usr/bin/env bash
# Signs a deployed logdor.app from the inside out.
#
#   sign.sh <app> [identity]
#
# With no identity (or "-") the bundle is ad-hoc signed. That is the minimum
# Apple Silicon Macs need to run it at all; Gatekeeper still asks the user to
# allow it. A "Developer ID Application" identity also turns on the hardened
# runtime and a secure timestamp, which notarization requires.
#
# codesign --deep can't be used: it skips Contents/plugins (not a standard
# nested-code location), and it can't give QtWebEngineProcess its
# entitlements.
set -euo pipefail

app=$1
identity=${2:--}

opts=(--force --sign "$identity")
if [[ $identity != - ]]; then
    opts+=(--options runtime --timestamp)
fi

# Leaf code first: Logdor's plugins, Qt's plugins, liblogdor_interface.
find "$app/Contents" -type f -name '*.dylib' -print0 |
    xargs -0 codesign "${opts[@]}"

# QtWebEngineProcess runs V8, which under the hardened runtime needs the
# JIT entitlements Qt ships inside the helper.
find "$app/Contents/Frameworks" -type d -path '*/Helpers/*.app' -prune -print0 |
    while IFS= read -r -d '' helper; do
        ent=$(find "$helper/Contents/Resources" -name '*.entitlements' | head -n 1)
        codesign "${opts[@]}" ${ent:+--entitlements "$ent"} "$helper"
    done

find "$app/Contents/Frameworks" -maxdepth 1 -type d -name '*.framework' -print0 |
    xargs -0 codesign "${opts[@]}"

codesign "${opts[@]}" "$app"
codesign --verify --deep --strict --verbose=2 "$app"
