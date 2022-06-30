from matplotlib import pyplot as plt

plt.style.use('seaborn-whitegrid')

import numpy as np
import os


os.system("ns simple_tcp.tcl")

with open("WinFile", "r") as f:
    lines = f.readlines();
    
cwndvstime = {}
   
for line in lines:
    split_line = line.split();
    cwndvstime[float(split_line[0])] = float(split_line[1]);


plt.plot(cwndvstime.keys(), cwndvstime.values() );
plt.xlabel("time")
plt.ylabel("cwnd")
plt.show()