# Makefile for shared-core CLI

SRC = fol_tool.cpp ../core/src/fol_core.cpp
NAME = fol_tool

# 本地编译 (macOS M1)
CC_LOCAL = clang++
CFLAGS_LOCAL = -O3 -Wall -Wextra -std=c++20 -I../core/include

# Windows 交叉编译 (需要 brew install mingw-w64)
CC_WIN = x86_64-w64-mingw32-g++
CFLAGS_WIN = -O3 -s -static -std=c++20 -I../core/include

# Linux 交叉编译 
# 注意: 这里仍然使用一个非常基础的交叉编译入口
CC_LINUX = x86_64-elf-g++
CFLAGS_LINUX = -O3 -s -static -std=c++20 -I../core/include

# --- 目标 ---

.PHONY: all clean mac windows linux

all: mac windows

# 1. macOS (本机 Arm64)
mac: $(SRC)
	@echo "Compiling for macOS (Arm64)..."
	$(CC_LOCAL) $(CFLAGS_LOCAL) -o $(NAME)_mac $(SRC)
	@echo "Output: $(NAME)_mac"
	@ls -lh $(NAME)_mac

# 2. Windows (x64)
windows: $(SRC)
	@echo "Compiling for Windows (x64)..."
	@if command -v $(CC_WIN) > /dev/null; then \
		$(CC_WIN) $(CFLAGS_WIN) -o $(NAME).exe $(SRC); \
		echo "Output: $(NAME).exe"; \
		ls -lh $(NAME).exe; \
	else \
		echo "[!] Skip Windows: $(CC_WIN) not found. Run 'brew install mingw-w64'"; \
	fi

# 3. Linux (x64)
# 注意: 这在 Mac 上通常会失败，除非你有完整的 elf 工具链和 libc
linux: $(SRC)
	@echo "Compiling for Linux (x64)..."
	@if command -v $(CC_LINUX) > /dev/null; then \
		$(CC_LINUX) $(CFLAGS_LINUX) -o $(NAME)_linux $(SRC); \
		echo "Output: $(NAME)_linux"; \
	else \
		echo "[!] Skip Linux: Compiler not found."; \
	fi

clean:
	rm -f $(NAME)_mac $(NAME).exe $(NAME)_linux
