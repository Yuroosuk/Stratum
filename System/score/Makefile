CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
TARGET   := score
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)