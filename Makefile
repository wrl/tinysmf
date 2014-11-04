all:
	mkdir -p build
	gcc -std=c99 -Wall -Werror -Wextra -Wcast-align -Wno-unused-parameter -pedantic src/parse.c src/test.c -Iinclude -o build/smf

clean:
	rm -rf build
