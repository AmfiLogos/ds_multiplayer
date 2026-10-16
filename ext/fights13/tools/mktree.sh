#!/bin/zsh
# Test tree for lane fights before units13 is on main: units13's committed tree (home, units, build lists, cm_module.c)
# + this worktree's custom/fights and custom/gallery. Output: <scratch>/tree. usage: mktree.sh <scratch dir> [units branch]
set -e
S=${1:A}; UB=${2:-units13}; W=${0:A:h:h:h:h}
mkdir -p $S; [[ -d $S/tree ]] && mv $S/tree $S/old_tree_$(date +%s)
mkdir -p $S/tree
git -C ~/ds_multiplayer archive $UB custom sdk src | tar -x -C $S/tree
for d in fights gallery; do mkdir -p $S/tree/custom/$d; rsync -a --delete --exclude test $W/custom/$d/ $S/tree/custom/$d/; done
echo "tree: $S/tree (units from $(git -C ~/ds_multiplayer rev-parse --short $UB))"
