#!/bin/zsh
# bg_test.sh <test.py> <tag> [args]: start a PvP pair test detached (log ext/pvp/out/<tag>.log), then the caller waits in
# foreground loops with wait_test.sh (night rule 7: a background job alone does not wake the lane).
R=~/ds_multiplayer; T=$1; TAG=$2; shift 2
cd $R && nohup /opt/homebrew/bin/python3 ext/pvp/tests/$T --tag $TAG "$@" > ext/pvp/out/$TAG.log 2>&1 &
echo "started $T as $TAG pid $!"
