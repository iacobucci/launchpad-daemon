#include <alsa/asoundlib.h>
#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void handle_alsa_error(const char *msg, int err) {
	fprintf(stderr, "%s: %s\n", msg, snd_strerror(err));
	exit(EXIT_FAILURE);
}

int cells[8][8] = {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
				   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
				   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
				   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}};

#define coords(x, y) (y + x * y)

void hue(float x) {
	int pid = fork();

	char s[10];

	if (x == 0) {
		sprintf(s, "0");
	} else {
		sprintf(s, "%f", x);
	}

	if (pid == 0) {
		execl("/home/valerio/script/hue", "hue", s, NULL);
	}

	wait(NULL);
}

struct status {
	int connected;
	int power;
	float brightness;
};

int parse_status(const char *json_str, struct status *s) {
	cJSON *json = cJSON_Parse(json_str);

	if (json == NULL)
		return -1;

	cJSON *connected = cJSON_GetObjectItemCaseSensitive(json, "connected");
	cJSON *power = cJSON_GetObjectItemCaseSensitive(json, "power");
	cJSON *brightness = cJSON_GetObjectItemCaseSensitive(json, "brightness");

	if (!cJSON_IsBool(connected) || !cJSON_IsBool(power) ||
		!cJSON_IsNumber(brightness)) {
		cJSON_Delete(json);
		return -1;
	}

	s->connected = cJSON_IsTrue(connected);
	s->power = cJSON_IsTrue(power);
	s->brightness = (float)brightness->valuedouble;

	cJSON_Delete(json);
	return 0;
}

struct status get_status() {
	char buffer[1024];

	FILE *fp = popen("hue status", "r");

	if (fp == NULL) {
		perror("popen failed");
		exit(1);
	}

	fread(buffer, sizeof(char), sizeof(buffer) - 1, fp);
	pclose(fp);
	buffer[sizeof(buffer) - 1] = '\0';

	struct status s;
	if (parse_status(buffer, &s) == 0) {
		printf("Connected: %d\nPower: %d\nBrightness: %.2f\n", s.connected,
			   s.power, s.brightness);
	} else {
		fprintf(stderr, "Failed to parse JSON\n");
		s.connected = 0;
		s.power = 0;
		s.brightness = 0;
		return s;
	}
	return s;
}

int small_dist(float x, float y) {
	float q = fabsf(x - y);
	return (q <= 0.05);
}

void hue_or_power_off(float x) {
	struct status s = get_status();
	if (small_dist(x, s.brightness)) {
		if (s.power)
			hue(0);
		else {
			hue(x);
		}
	} else {
		hue(x);
	}
}

void scripts(int x, int y) {
	switch (coords(x, y)) {
	case coords(0, 0):
		hue_or_power_off(1);
		break;
	case coords(0, 1):
		hue_or_power_off(0.25);
		break;
	}
}

void debug_cells() {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			printf("%i ", cells[y][x]);
		}
		printf("\n");
	}
	printf("\n");
}

int find_launchpad(char *launchpad_id, size_t id_size) {
	int card = -1;

	if (snd_card_next(&card) < 0 || card < 0) {
		fprintf(stderr, "Nessuna scheda audio trovata\n");
		return 1;
	}

	while (card >= 0) {
		snd_ctl_t *ctl;
		char ctl_name[32];
		sprintf(ctl_name, "hw:%d", card);

		if (snd_ctl_open(&ctl, ctl_name, 0) < 0) {
			snd_card_next(&card);
			continue;
		}

		int device = -1;
		while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0) {
			snd_rawmidi_info_t *info;
			snd_rawmidi_info_alloca(&info);

			snd_rawmidi_info_set_device(info, device);
			snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
			snd_rawmidi_info_set_subdevice(info, 0);

			if (snd_ctl_rawmidi_info(ctl, info) >= 0) {
				const char *name = snd_rawmidi_info_get_name(info);
				if (name && strstr(name, "Launchpad Mini")) {
					snprintf(launchpad_id, id_size, "hw:%d,%d", card, device);
					snd_ctl_close(ctl);
					return 0;
				}
			}
		}

		snd_ctl_close(ctl);
		snd_card_next(&card);
	}

	return 1;
}

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
	char id[32];

	if (find_launchpad(id, sizeof(id))) {
		printf("cant find launchpad");
		return 1;
	}

	const char *input_port_name = id;
	const char *output_port_name = id;

	snd_rawmidi_t *midi_in = NULL;
	snd_rawmidi_t *midi_out = NULL;
	int err;

	if ((err = snd_rawmidi_open(&midi_in, NULL, input_port_name,
								SND_RAWMIDI_NONBLOCK)) < 0) {
		handle_alsa_error("Impossibile aprire la porta MIDI di input", err);
	}

	if ((err = snd_rawmidi_open(NULL, &midi_out, output_port_name, 0)) < 0) {
		snd_rawmidi_close(midi_in);
		handle_alsa_error("Impossibile aprire la porta MIDI di output", err);
	}

	printf("In ascolto su input: %s, Output su: %s. Premi Ctrl+C per uscire.\n",
		   input_port_name, output_port_name);

	unsigned char input_buffer[32];
	unsigned char output_buffer[3];
	ssize_t n_read_bytes;

	while (1) {
		n_read_bytes =
			snd_rawmidi_read(midi_in, input_buffer, sizeof(input_buffer));

		if (n_read_bytes < 0) {
			if (n_read_bytes == -EAGAIN) {
				usleep(10000); // Attendi un breve periodo
				continue;
			}
			fprintf(stderr, "Errore durante la lettura dalla porta MIDI: %s\n",
					snd_strerror((int)n_read_bytes));
			break;
		}

		if (n_read_bytes > 0) {

			// for (ssize_t i = 0; i < n_read_bytes; ++i) {
			// 	printf("%02X ", input_buffer[i]);
			// }
			// printf("\n");

			for (ssize_t i = 0; (i + 2) < n_read_bytes;) {
				unsigned char status_byte = input_buffer[i];
				unsigned char data_byte1 = input_buffer[i + 1];
				unsigned char data_byte2 = input_buffer[i + 2];

				// Controlla se è un messaggio Note On (0x9n) sul canale 0
				// (0x90) I Launchpad spesso usano il canale 0 (indice 0) per i
				// pad principali. Potrebbe essere necessario adattare il canale
				// (0x90 - 0x9F)
				if ((status_byte & 0xF0) == 0x90 ||
					(status_byte & 0xF0) == 0xB0) { // Note On
					unsigned char note_number = data_byte1;
					unsigned char velocity = data_byte2;

					if (velocity > 0) { // Tasto premuto

						int y = (data_byte1 >> 1) / 8;
						int x = (data_byte1) % 8;

						cells[y][x] = !cells[y][x];

						output_buffer[0] = (status_byte & 0xF0);
						// messaggio ricevuto)
						output_buffer[1] =
							note_number; // Stessa nota del tasto premuto
						output_buffer[2] = 0x0E;
						// (cells[y][x])
						// 	? 0x0E
						// 	: 0x00; // switch

						if ((err = snd_rawmidi_write(midi_out, output_buffer,
													 3)) < 0) {
							fprintf(stderr,
									"Errore durante l'invio del messaggio "
									"MIDI: %s\n",
									snd_strerror(err));
						} else if (err < 3) {
							fprintf(
								stderr,
								"Errore: Inviati solo %d byte invece di 3\n",
								err);
						}

						scripts(x, y);

						// debug_cells();
					} else { // Tasto rilasciato (Note On con velocity 0 è
						// spesso un Note Off)
						// Spegni il LED corrispondente
						output_buffer[0] = (status_byte & 0xF0);
						output_buffer[1] = note_number; // Stessa nota
						output_buffer[2] = 0x00; // Velocity 0 (LED spento)

						// // printf("    Invio MIDI Out (LED OFF): %02X %02X
						// // %02X\n", 	   output_buffer[0], output_buffer[1],
						// // 	   output_buffer[2]);

						if ((err = snd_rawmidi_write(midi_out, output_buffer,
													 3)) < 0) {
							fprintf(stderr,
									"Errore durante l'invio del messaggio MIDI "
									"(LED OFF): %s\n",
									snd_strerror(err));
						}
					}
					i += 3; // Avanza al prossimo messaggio MIDI (assumendo
							// messaggi da 3 byte)
				} else {
					// Messaggio non gestito o frammento, avanza di 1 byte e
					// riprova Un parser robusto gestirebbe lo "stato corrente"
					// MIDI.
					i++;
				}
			}
		}
	}

	// Chiudi le porte MIDI
	if (midi_in) {
		snd_rawmidi_close(midi_in);
	}
	if (midi_out) {
		snd_rawmidi_drain(
			midi_out); // Attendi che tutti i messaggi in uscita siano inviati
		snd_rawmidi_close(midi_out);
	}

	return 0;
}