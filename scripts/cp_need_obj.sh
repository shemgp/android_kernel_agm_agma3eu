#! /bin/bash

oldext="o"
newext="obj"

OUT=kout

cp ./$OUT/drivers/sensors/sensors_class.o  ./drivers/sensors/sensors_class.obj
cp -r ./$OUT/drivers/sensors  ./drivers/sensors
rm -rf ./drivers/sensors/built-in.o
rm -rf ./drivers/sensors/modules.builtin
rm -rf ./drivers/sensors/modules.order

rm -rf ./drivers/sensors/*.c
rm -rf ./drivers/sensors/*.h
rm -rf ./drivers/sensors/sensors_class.c

cd ./drivers/sensors/
dir=$(eval pwd)

for file in $(ls $dir | grep -E "\.$oldext$")
	do
	name=$(ls $file | cut -d. -f1)
	mv $file ${name}.$newext
	echo "$name.$oldext ====> $name.$newext"
	done

cd ../../../
rm -rf ./drivers/sensors/*.o
rm -rf ./drivers/sensors/*.o

