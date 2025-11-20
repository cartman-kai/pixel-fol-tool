# Makefile for FOL Tool

# --- 配置 ---
SRC = fol_tool.c
NAME = fol_tool

# 本地编译 (macOS M1)
CC_LOCAL = clang
CFLAGS_LOCAL = -O3 -Wall -Wextra

# Windows 交叉编译 (需要 brew install mingw-w64)
CC_WIN = x86_64-w64-mingw32-gcc
CFLAGS_WIN = -O3 -s -static -finput-charset=UTF-8 -fexec-charset=UTF-8

# Linux 交叉编译 
# 注意: 在 macOS 上完美交叉编译带 glibc 的 Linux 程序很困难。
# 这里假设你安装了 x86_64-elf-gcc 并且只做最基础的编译，
# 或者你可以将此命令替换为 docker run ...
CC_LINUX = x86_64-elf-gcc
CFLAGS_LINUX = -O3 -s -static

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