#!/usr/bin/bash -e

lsof -t -iTCP:55555 -sTCP:LISTEN | xargs -r kill -9

cmake --build build&
[[ -f echo/other/echo-go && -f echo/other/target/release/echo-rust ]] || (cd echo/other && make)&
wait

shopt -s nullglob
SERVERS=($(dirname "$0")/../build/echo/echo_*)
SERVERS+=(echo/other/echo-go)
SERVERS+=(echo/other/target/release/echo-rust)

PATTERN='at ([0-9]+) MB/s'
for ECHO in "${SERVERS[@]}"
do
   "$ECHO" >/dev/null 2>&1 &
   sleep 0.5   
   [[ $(build/bin/client -c 1 -d 1|grep Total) =~ $PATTERN ]]
   N=${BASH_REMATCH[1]}
   printf '%04d ' $N
   for ((i = 0; i < N/100; i++)); do echo -n '='; done
   echo " ${ECHO##*/}"
   kill $!
   wait
done
