xrequests:
	g++ xrequests.cpp -Wall -std=c++14 -Iinclude -o xrequests -pthread -lcurl

all: xrequests

clean:
	rm -rf xrequests
