#!/bin/bash

serviceName="lab"
pathToExec="/home/key/projects/unix/unix_process_manager_ IPC/bin/lab"
log="/home/key/log.txt"
exec="bc"
ftok="/home/key/tok"
multiplex=1

case ${1} in
     start)
		pid=$(ps -a | grep $serviceName | awk '{print $1}')
		if [ "$pid" != "" ]
		then
			echo "Service already running. pid: $pid"
		else
			nohup "$pathToExec" -f "$ftok" -e "$exec" -l "$log" -m "$multiplex" >/dev/null &
			sleep 1
			pid=$(ps -a | grep $serviceName | awk '{print $1}')
			if [ "$pid" != "" ]
			then
				echo "Service starting: success. pid: $pid"
			else
				echo "Service starting: fail."
			fi
		fi		
     ;;
     status)
		pid=$(ps -a | grep $serviceName | awk '{print $1}')
		if [ "$pid" != "" ]
		then
			echo "Service is running. pid: $pid"
		else
			echo "Service is not running"
		fi
     ;;
     stop)
		pid=$(ps -a | grep $serviceName | awk '{print $1}')
		if [ "$pid" != "" ]
		then
			kill -2 $pid
			pid=$(ps -a | grep $serviceName | awk '{print $1}')
			if [ "$pid" != "" ]
			then
				echo "Service stopping: fail."
			else
				echo "Service stopping: success."
			fi
		else
			echo "Service is not running"
		fi
     ;;
     *)
		echo "Unknown command"
		echo "Use ${0} start|stop|status"
        exit
     ;;
esac 
