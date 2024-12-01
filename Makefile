CXX = g++
CXXFLAGS = -std=c++17 -Wall
LDFLAGS =

SRCS = main.cpp server.cpp  logger.cpp utils.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = web-server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)
