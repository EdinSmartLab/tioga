#!/usr/bin/env bash
set -e  # do not continue if any command fails

HELP_STRING="Usage: $0 <prefix> <mesh> <is_setup>"
if [[ $# -ne 3 ]]; then
  echo -e $HELP_STRING
  exit 0
fi
prefix=$1
mesh=$2
setup=$3

result="["
for i in {1000,2000,4000,8000,16000,32000}; do
    if   [ x"$mesh" == x"1" ] && [ x"$setup" == x"1" ]; then
        value="$(grep '>>>' logs/${prefix}_np1_$i.log  | cut -f 4 -d ' ' | head -n 1)"
    elif [ x"$mesh" == x"1" ] && [ x"$setup" == x"0" ]; then
        value="$(grep '>>>' logs/${prefix}_np1_$i.log  | cut -f 4 -d ' ' | head -n 2 | tail -n 1)"
    elif [ x"$mesh" == x"2" ] && [ x"$setup" == x"1" ]; then
        value="$(grep '>>>' logs/${prefix}_np1_$i.log  | cut -f 4 -d ' ' | head -n 3 | tail -n 1)"
    elif [ x"$mesh" == x"2" ] && [ x"$setup" == x"0" ]; then
        value="$(grep '>>>' logs/${prefix}_np1_$i.log  | cut -f 4 -d ' ' | head -n 4 | tail -n 1)"
    fi

    if [ "$i" == "1000" ]; then
        result="$result $value"
    else
        result="$result, $value"
    fi
done
result="$result]"
echo $result
