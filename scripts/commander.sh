#!/bin/bash

# Central commander script for multi NFS clients

machines=(
    sgdp05:nfs-client1
    sgdp05:nfs-client2
    sgdp05:nfs-client3
    sgdp05:nfs-client4
    sgdp04:nfs-client5
    sgdp04:nfs-client6
    sgdp04:nfs-client7
    sgdp04:nfs-client8
)

targets='0 1 2 3 4 5 6 7'
cmd=()
cmd_group=()
parallel=0
quiet=0

while [[ $# -gt 0 ]]; do
    key="$1";
    case $key in
        -t|--targets)
            targets="$2";
            shift;
            shift;
            ;;
        -p|--parallel)
            parallel=1;
            shift;
            ;;
        -c|--cmd)
            cmd_group+=("$2");
            shift;
            shift;
            ;;
        -q|--quiet)
            quiet=1
            shift;
            ;;
        *)
            cmd+=("$1");
            shift;
            ;;
    esac
done

if [ -z "$targets" ] || [ -z "$cmd" ] && [ "${#cmd_group}" -eq "0" ]; then
    echo "Too few arguments.";
    exit 1;
fi

if [ "${#cmd_group[@]}" -gt 0 ] && [ "${#cmd_group[@]}" -ne "`echo $targets | wc -w`" ]; then
    echo "When using --cmd to specify individual commands, the number of --cmd parameters"
    echo "should be the same as that of target machines.";
    echo "$(echo $targets | wc -w) targets, ${#cmd_group[@]} commands.";
    echo ${cmd_group[@]};
    exit 2;
fi

count=0
for i in $targets; do
    str=${machines[i]}
    master=`echo $str | cut -d ':' -f 1`;
    secondary=`echo $str | cut -d ':' -f 2`;
    if [ "$quiet" != "1" ]; then
        echo "Connecting $master:$secondary:";
    fi
    if [ "${#cmd_group}" -gt 0 ]; then
        remote_command="${cmd_group[$count]}";
        let count++;
    else
        remote_command="${cmd[@]}";
    fi

    if [ "$parallel" = "1" ]; then
        ssh -q -t $master "uvt-kvm ssh $secondary '$remote_command'" &
    else
        ssh -q -t $master "uvt-kvm ssh $secondary '$remote_command'";
    fi
done

if [ "$parallel" = "1" ]; then
    wait;
fi

