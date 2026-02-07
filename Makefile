# 定义变量
CC = gcc
CFLAGS = -Wall -c
LIBS = -lncurses -lcjson
TARGET = tmcl
SOURCES = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,build/%.o,$(SOURCES))

# 默认目标
all: $(TARGET)

# 链接目标文件生成可执行文件
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

# 编译每个 .c 文件为 .o 文件
build/%.o: src/%.c
	$(CC) $(CFLAGS) $< -o $@

# 清理生成的文件
clean:
	rm -f $(OBJS) $(TARGET)

# 安装（示例）
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# 卸载
	rm /usr/local/bin/tmcl

# 声明伪目标（不对应实际文件）
.PHONY: all clean install
