CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

all: server subscriber

server: server.cpp
	$(CXX) $(CXXFLAGS) -o server server.cpp

subscriber: subscriber.cpp
	$(CXX) $(CXXFLAGS) -o subscriber subscriber.cpp

clean:
	rm -f server subscriber
