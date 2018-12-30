# rdc
reliable distributed communication  

RDC-CI Status: [![Travis-CI Status](https://travis-ci.org/akkaze/rdc.svg?branch=master)](https://travis-ci.org/akkaze/rdc)  

RDC is an EXPERIMENTAL C++ framework for distributed computations with mpi-like interface and easy-used fault tolerance functionality which mpi does not offer.  
## Installation  
Just run ./install.sh  
#### A very simple example with python  
```python
#!/usr/bin/env python
import rdc
import numpy as np

rdc.init()
comm = rdc.new_comm('main')
if rdc.get_rank() == 0:
    s = 'hello'
    buf = rdc.Buffer(s.encode())
    comm.send(buf, 1)
elif rdc.get_rank() == 1:
    s = '00000'
    buf = rdc.Buffer(s.encode())
    comm.recv(buf, 0)
    print(buf.bytes())
rdc.finalize()
```
Save these codes as sendrecv.py, To run this program on two nodes, just type `python -m tracker.lancher_local -n 2 sendrecv.py` in terminal
