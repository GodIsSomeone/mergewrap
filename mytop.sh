#!/bin/sh

if [ $1 ];then
    echo $1
    pid=`ps -e | grep $1 | cut -f 1 -d ' '`
    echo "pid is $pid"
    top -p $pid
else
    echo "$0 <program>"
fi

