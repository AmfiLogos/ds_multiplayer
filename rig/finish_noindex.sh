#!/bin/zsh
# Move A and B into instances.noindex (Spotlight skips *.noindex) and make "instances" a symlink to it.
# Refuses while A or B runs. C-F were moved already (2026-10-12).
set -e; cd ~/ds_multiplayer
for X in A B; do
  pid=$(cat instances/$X/game.pid 2>/dev/null || true)
  if [[ -n $pid ]] && kill -0 $pid 2>/dev/null; then echo "$X is running (pid $pid): stop it first" >&2; exit 1; fi
done
for X in A B; do [[ -L instances/$X ]] || mv instances/$X instances.noindex/$X; done
mkdir -p old_versions; mv instances old_versions/instances-symlinks-$(date +%Y-%m-%d-%H%M)
ln -s instances.noindex instances
ls -la instances/
