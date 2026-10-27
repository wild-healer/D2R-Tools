#!/usr/bin/env bash
# Pull the latest D2R-Tools and make grail.html readable by nginx again.
# Usage: ./deploy/update.sh   (run as the repo owner; asks for sudo for the SELinux step)
set -euo pipefail

REPO=/home/fedora/D2R-Tools
OWNER=fedora

# git as the repo owner, so root never touches the checkout
if [[ $(id -un) == "$OWNER" ]]; then
    git -C "$REPO" pull --ff-only
else
    sudo -u "$OWNER" git -C "$REPO" pull --ff-only
fi

# nginx needs to traverse the home dir and read the page
chmod o+x /home/fedora "$REPO"
chmod 644 "$REPO/grail.html"

# SELinux label (the rule from the setup steps; restorecon reapplies it)
sudo restorecon -v "$REPO/grail.html"

echo "Deployed $(git -C "$REPO" log -1 --format='%h %s')"
