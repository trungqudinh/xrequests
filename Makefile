xrequests:
	g++ xrequests.cpp -Wall -std=c++14 -Iinclude -o xrequests -pthread -lcurl -ljsoncpp

all: xrequests

clean:
	rm -rf xrequests
