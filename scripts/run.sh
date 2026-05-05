target=$1
node_count=$2

rm /dev/shm/rac 2> /dev/null
for j in $(seq 0 $((${node_count}-1))); do
    echo "launch server $j"
    RAC_SERVER_IDX=$j ./$target &
done
wait

