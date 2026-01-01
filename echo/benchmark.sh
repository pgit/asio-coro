#!/usr/bin/bash -e
#
# Script for comparing performance of echo server implementations using bin/client.
# Uses localhost port 55555, which is the default of most of the examples in this repository.
#
CLIENT=(build/bin/client "$@")

cmake --build build&
[[ -f echo/other/echo-go && -f echo/other/target/release/echo-rust ]] || (cd echo/other && make)&
wait

# kill any leftover processes
lsof -t -iTCP:55555 -sTCP:LISTEN | xargs -r kill -9

# collect echo implementations from 'echo' subdir, including 'go' and 'rust' ones
shopt -s nullglob
P=$(dirname "$0")/..
SERVERS=("$P"/build/echo/echo_{sync,async,coro}*) 
SERVERS+=("$P"/echo/other/{echo-go,target/release/echo-rust})

WIDTH=80
MAX=10000
PATTERN='at ([0-9]+) MiB/s'
for ECHO in "${SERVERS[@]}"
do
   "$ECHO" >/dev/null 2>&1 &
   build/bin/wait_for_port
   [[ $("${CLIENT[@]}"|grep Total) =~ $PATTERN ]] || "${CLIENT[@]}"
   N=${BASH_REMATCH[1]}
   printf '%5d ' $N
   [[ ${ECHO} = *_sync*  ]] && echo -ne "\x1b[1;31m"
   [[ ${ECHO} = *_async* ]] && echo -ne "\x1b[1;33m"
   [[ ${ECHO} = *_coro*  ]] && echo -ne "\x1b[1;32m"
   [[ ${ECHO} = *-go*    ]] && echo -ne "\x1b[1;34m"
   [[ ${ECHO} = *-rust*  ]] && echo -ne "\x1b[38;5;166m"
   for ((i = 0; i < N*WIDTH/MAX; i++)); do echo -n '▆'; done
   echo -ne "\x1b[0m"
   for ((i = 0; i < WIDTH-N*WIDTH/MAX; i++)); do echo -n '·'; done
   echo " ${ECHO##*/}"
   kill $!
   wait
done
