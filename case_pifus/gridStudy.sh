#!/usr/bin/env bash
set -e  # do not continue if any command fails

HELP_STRING="Usage: $0 <num_mpi_ranks>"
if [[ $# -ne 1 ]]; then
  echo -e $HELP_STRING
  exit 0
fi

num_mpi_ranks=$1
num_mesh_parts=$(($num_mpi_ranks * 2))

echo "#proc       $num_mpi_ranks"
echo "#mesh_parts $num_mesh_parts"
echo $num_mesh_parts
for i in {1000,2000,4000,8000,16000,32000}; do
    echo "<<< $i >>>"

    cd grid
    cp sphere_files/sphere_$i.facet ./data.tri
    cp input_files/input_$i ./input
    head -1 data.tri
    head -2 input
    mpirun -np $num_mesh_parts ../../build/gridGen/buildGrid
    cd ..

    mpirun --bind-to core --map-by socket -np $num_mpi_ranks ../build/driver/tioga.exe | tee tioga_np${num_mpi_ranks}_$i.log
done
