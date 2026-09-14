CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g
LDFLAGS := -lreadline -lncurses

TARGET   := score
SRCS     := $(shell find src -name '*.cpp')
OBJS     := $(SRCS:.cpp=.o)


$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)