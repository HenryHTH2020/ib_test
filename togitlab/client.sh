#!/bin/bash

./kill_V1client.sh
make clean
make
echo pid
echo $$
echo pidover

for((total=1;total<=1;total*=2))
do
echo "*********************************"
echo proc_qtt is $total
./sync_res_init
for((i=0;i<$total;i++))


do


echo "**********"
echo $(date +%Y-%m-%d\ %H:%M:%S)
echo proc_num $i of $total
./V1_MP --proc_ind $i --proc_qtt $total -c UD --shr_mr &


done



wait
./sync_res_destroy

done

