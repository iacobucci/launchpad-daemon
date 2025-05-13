.PHONY: restart main watch

restart:
	make main
	./midi_listener hw:2,0,0

main:
	gcc main.c -o midi_listener -lasound

watch:
	find . -name '*.c' -o -name '*.h' | entr -rz make restart