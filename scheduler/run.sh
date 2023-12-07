make

echo "**Running FCFS homogeneous.txt**"
./scheduler FCFS homogeneous.txt
echo "**Running RR homogeneous.txt**"
./scheduler RR 1000 homogeneous.txt

echo "**Running FCFS reverse.txt**"
./scheduler FCFS reverse.txt
echo "**Running RR reverse.txt**"
./scheduler RR 1000 reverse.txt

echo "**Running FCFS mixed.txt**"
./scheduler_io FCFS mixed.txt
echo "**Running RR mixed.txt**"
./scheduler_io RR 1000 mixed.txt

