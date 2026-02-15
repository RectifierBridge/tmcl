# 定义变量
CC = gcc
CFLAGS = -Wall -c
LIBS = -lncurses -lcjson
TARGET = tmcl

# 自动找到所有 src 下的 .c 文件（含多级子目录）
SOURCES := $(shell find src -name "*.c")
OBJS := $(patsubst src/%.c, build/%.o, $(SOURCES))

# 默认目标
all: $(TARGET)

# 链接生成可执行文件
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

# 【关键】自动创建 build 下的所有子目录，再编译
build/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

# 清理：直接删除整个 build 目录
clean:
	rm -rf build $(TARGET)

# 安装
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# 卸载
uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall
