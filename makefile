.PHONY: restart main watch

main:
	gcc main.c -o midi_listener -lasound -lcjson -lm

install:
	mkdir -p ~/.local/bin
	cp midi_listener ~/.local/bin

restart:
	make main
	./midi_listener

watch:
	find . -name '*.c' -o -name '*.h' | entr -rz make restart

systemd-reload:
	make
	systemctl --user stop launchpad-daemon.service
	make install
	systemctl --user daemon-reload
	systemctl --user restart launchpad-daemon.service
