# Web Proxy
A multithreaded web proxy that can manage hundreds of clients and handle HTTP requests.

# Usage
To set up the proxy you simply need to compile it and pass in the port number you wish
to use upon running it.

Example of running the proxy on port# 10000 using g++ compiler on unix platform:
g++ -o proxy myproxy.cpp
./proxy 10000

If you wish to connect firefox to your proxy:
1. Go to the 'Edit' menu.
2. Select 'Preferences'. Select 'Advanced' and then select 'Network'.
3. Under 'Connection', select 'Settings...'.
4. Select 'Manual Proxy Configuration'. Enter the hostname and port where your proxy
program is running.
5. Save your changes by selecting 'OK' in the connection tab and then select 'Close'
in the preferences tab.

# Dependencies
Utalizes only C++ 11 standard libraries

# Author
Richard Charczenko
