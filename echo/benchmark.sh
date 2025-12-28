#!/usr/bin/bash -e

lsof -t -iTCP:55555 -sTCP:LISTEN | xargs -r kill -9

cmake --build build&
[[ -f echo/other/echo-go && -f echo/other/target/release/echo-rust ]] || (cd echo/other && make)&
wait

shopt -s nullglob
SERVERS=($(dirname "$0")/../build/echo/echo_*)
SERVERS+=(echo/other/echo-go)
SERVERS+=(echo/other/target/release/echo-rust)

PATTERN='at ([0-9]+) MB'
for ECHO in "${SERVERS[@]}"
do
   "$ECHO" >/dev/null 2>&1 &
   sleep 0.5
   [[ $(build/bin/client -c 10 -d 10|grep Total) =~ $PATTERN ]]
   N=${BASH_REMATCH[1]}
   printf '%04d ' $N
   N=$((N / 100))
   for ((i = 0; i<N; i++)); do echo -n '='; done
   echo " ${ECHO##*/}"
   kill $!
   wait
done
