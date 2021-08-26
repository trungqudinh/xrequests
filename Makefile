BINDIR = build
APPS = xrequests
SOURCES = xrequests.cpp
CXX = g++ -Wall -std=c++14 -Iinclude 
LIBS = -pthread -lcurl -ljsoncpp
DESTDIR = /usr/local/bin/

all: $(APPS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(APPS): $(BINDIR)
	$(CXX) $(SOURCES) -o $(BINDIR)/$(APPS) $(LIBS)

clean:
	rm -rf $(BINDIR)

deb:
	@debuild -us -uc -b -d

install:
	install -m 0755 $(BINDIR)/$(APPS) $(DESTDIR)
