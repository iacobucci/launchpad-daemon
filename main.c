#include <alsa/asoundlib.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <math.h>

// Funzione per stampare l'errore ALSA e uscire
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

#define BUFFER_SIZE 1024

char *get_amidi_result() {
	int fd[2];
	if (pipe(fd) == -1) {
		perror("pipe failed");
		return NULL;
	}

	int pid = fork();

	if (pid == -1) {
		perror("fork failed");
		return NULL;
	}

	if (pid == 0) {
		// Figlio
		close(fd[0]); // Chiudi lettura

		// Redirigi stdout sulla pipe
		close(STDOUT_FILENO);		// close(1)
		dup2(fd[1], STDOUT_FILENO); // dup su 1
		close(fd[1]);				// chiudi duplicato

		execl("/usr/bin/amidi", "amidi", "-l", (char *)NULL);
		perror("execl failed");
		_exit(1);
	} else {
		// Padre
		close(fd[1]); // Chiudi scrittura

		char *output = malloc(BUFFER_SIZE);
		if (!output) {
			perror("malloc failed");
			return NULL;
		}
		output[0] = '\0'; // stringa vuota

		char temp[256];
		ssize_t count;
		size_t total_len = 0;

		while ((count = read(fd[0], temp, sizeof(temp) - 1)) > 0) {
			temp[count] = '\0';

			// Verifica se serve più memoria
			if (total_len + count + 1 >= BUFFER_SIZE) {
				// Puoi usare realloc dinamica, ma per ora evitiamo
				fprintf(stderr, "Output troppo grande per buffer\n");
				break;
			}

			strcat(output, temp);
			total_len += count;
		}

		close(fd[0]);
		wait(NULL);

		return output;
	}
}

char *get_midi_address() {
	char *s = get_amidi_result();

	char *line = strtok(s, "\n");

	while (line != NULL) {
		printf("%s", line);
		line = strtok(NULL, "\n");
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Utilizzo: %s <nome_porta_input/output_alsa>\n",
				argv[0]);
		fprintf(stderr, "Esempio: %s hw:1,0,0\n", argv[0]);
		fprintf(stderr,
				"Usa 'amidi -l' per elencare le porte MIDI disponibili.\n");
		return 1;
	}

	const char *input_port_name = argv[1];
	const char *output_port_name = argv[1];

	snd_rawmidi_t *midi_in = NULL;
	snd_rawmidi_t *midi_out = NULL;
	int err;

	// Apri la porta MIDI di input
	if ((err = snd_rawmidi_open(&midi_in, NULL, input_port_name,
								SND_RAWMIDI_NONBLOCK)) < 0) {
		handle_alsa_error("Impossibile aprire la porta MIDI di input", err);
	}

	// Apri la porta MIDI di output
	// Nota: SND_RAWMIDI_SYNC apre in modalità bloccante per l'output, che
	// spesso è OK. Potresti usare SND_RAWMIDI_NONBLOCK anche qui se necessario,
	// ma la gestione degli errori di scrittura diventa più complessa.
	if ((err = snd_rawmidi_open(NULL, &midi_out, output_port_name, 0)) <
		0) {						// 0 per modalità bloccante di default
		snd_rawmidi_close(midi_in); // Chiudi l'input se l'output fallisce
		handle_alsa_error("Impossibile aprire la porta MIDI di output", err);
	}

	printf("In ascolto su input: %s, Output su: %s. Premi Ctrl+C per uscire.\n",
		   input_port_name, output_port_name);

	unsigned char input_buffer[32]; // Buffer per i dati MIDI in input
	unsigned char output_buffer[3]; // Buffer per i dati MIDI in output (es.
									// Note On: 3 byte)
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
			// printf("Ricevuti %zd byte(s): ", n_read_bytes);

			// for (ssize_t i = 0; i < n_read_bytes; ++i) {
			// 	printf("%02X ", input_buffer[i]);
			// }
			// printf("\n");

			// Processa solo messaggi MIDI completi (solitamente 3 byte per Note
			// On/Off) Questo è un parsing MOLTO basilare. Un vero parser MIDI è
			// più complesso.
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