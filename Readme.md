# Optimizing Congestion Control Through Fair Queuing Detection

## Building

To build the code run the following:

```
cd src
sunifdef -r -UQUIC_PORT -UQUIC_PORT_LOCAL ./pcc/\*
make
```
If you change the code and want to rebuild it, it is important you have to run ``make clean && make``.

Please also compile our utility to analyze TCP congestion windows, which is called ```wintracker``` using ```go build -o wintracker wintracker.go```. It is required for the evaluation of results. Our go version is ```1.10.2```. 

## Experimenting

We use the network emulation library [```py-virtnet```](https://pypi.org/project/py-virtnet/) ([GitHub repository](https://github.com/CN-TU/py-virtnet)) for our experiments. Our python version is ```3.7.3```.

To run an experiment, use ```test.py```. For example, it can be used as follows:

    sudo python3 test.py --qdisc pfifo --buffer_size 50 --delay 25 --rate 50 --time 30

```--qdisc pfifo``` means that no Fair Queuing is used while ```--qdisc fq``` uses it. 

Use the ```--store_pcaps``` option to write pcaps of each flow, which can be used to create plots of bandwidth and delay. 
    
### Accuracy 

To see how accurate our mechanism to detect the presence of Fair Queuing is, run 

    sudo python3 test.py --run_scenario accuracy
        
### Systematic evaluation

To do a systematic evalution of *loss-based congestion control*, run

    sudo python3 test.py --run_scenario evaluation --only_iperf --competing_flow 
    
For a systematic evalution of *our algorithm*, run

    sudo python3 test.py --run_scenario evaluation
    
### Plotting

To create plots of bandwidth/delay run 

    ls pcaps/sender*.pcap | xargs -L 1 ./plot_rtt_and_bandwidth.py
