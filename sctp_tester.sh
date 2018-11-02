while read x; do echo "$x" | ncat --sctp -v -d 1 localhost 9090 ; done < protos-cmds
