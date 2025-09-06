# Simplified Makefile for tsvm

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O0 -g
LDFLAGS = -lm
TARGET = tsvm
BUILD_DIR = build

# 所有源文件
SOURCES = main.c parser.c vmext.c \
          jass/vm_main.c jass/vm_compiler.c jass/jass_parser.c

# 头文件路径
INCLUDES = -I. -Iapi -Ijass

# 自动生成目标文件路径
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.c=.o)))

# 默认目标
all: $(TARGET)

# 创建构建目录并编译
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: jass/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

# 链接
$(TARGET): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)

# 运行（支持传参）
run: $(TARGET)
	./$(TARGET) $(ARGS)

# 清理
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean run