clang -std=c11 -pedantic-errors \
	-g -gdwarf-4 \
    -Wall -Wextra -Wno-unused-function -Wconversion \
	src/platform_linux.c \
    -o out/compilerc
