#!/bin/bash

rename 's/VT6/VT18/' time-absfs-yifeilatest*-VT6-pan*.csv

for file in time-absfs-yifeilatest4-VT18-pan*.csv; do
    # Extract the number before .csv using awk
    num=$(echo "$file" | awk -F'pan' '{print $2}' | cut -d '.' -f1)
    
    # Increment the number by 6
    new_num=$((num + 6))
    
    # Construct the new filename using bash's string replacement
    new_file="${file/pan$num.csv/pan$new_num.csv}"
    
    # Rename the file
    mv "$file" "$new_file"
done


for file in time-absfs-yifeilatest5-VT18-pan*.csv; do
    # Extract the number before .csv using awk
    num=$(echo "$file" | awk -F'pan' '{print $2}' | cut -d '.' -f1)
    
    # Increment the number by 12
    new_num=$((num + 12))
    
    # Construct the new filename using bash's string replacement
    new_file="${file/pan$num.csv/pan$new_num.csv}"
    
    # Rename the file
    mv "$file" "$new_file"
done

rename 's/yifeilatest4/yifeilatest3/' time-absfs-yifeilatest4-VT18-pan*.csv
rename 's/yifeilatest5/yifeilatest3/' time-absfs-yifeilatest5-VT18-pan*.csv

# 18 VTs, 13 hours
./multi_analyze_all.py -m 13 -n 18 > results-yifeilatest245-Overall-VT18-each6-13hours.csv
