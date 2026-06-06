# 定义变量
CC = gcc
CFLAGS = -Wall -c -Isrc -Isrc/account -Isrc/config -Isrc/version -Isrc/home
LIBS = -lncurses -lcjson
TARGET = tmcl

# 查找所有源文件（包括子目录）
SOURCES := $(wildcard src/*.c) \
           $(wildcard src/home/*.c) \
           $(wildcard src/account/*.c) \
           $(wildcard src/config/*.c) \
           $(wildcard src/version/*.c)

# 生成对应的目标文件路径
OBJS := $(patsubst src/%,build/%,$(SOURCES:.c=.o))

# 默认目标
all: $(TARGET)

# 链接目标文件生成可执行文件
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

# 编译每个 .c 文件为 .o 文件
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

# 清理生成的文件
clean:
	rm -rf build/* $(TARGET)

# 安装
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# 卸载
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# 声明伪目标
.PHONY: all clean install uninstall
