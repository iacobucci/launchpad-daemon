#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Funzione per stampare l'errore ALSA e uscire
void handle_alsa_error(const char *msg, int err) {
	fprintf(stderr, "%s: %s\n", msg, snd_strerror(err));
	exit(EXIT_FAILURE);
}

int cells[8][8] = {{0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
				   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
				   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
				   {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}};

void debug_cells() {
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			printf("%i ", cells[y][x]);
		}
		printf("\n");
	}
	printf("\n");
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

			for (ssize_t i = 0; i < n_read_bytes; ++i) {
				printf("%02X ", input_buffer[i]);
			}
			printf("\n");

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

					// printf("  -> Note On: Nota=%02X (%d), Vel=%02X (%d), "
					// 	   "Canale=%d\n",
					// 	   note_number, note_number, velocity, velocity,
					// 	   status_byte & 0x0F);

					if (velocity > 0) { // Tasto premuto

						int y = (data_byte1 >> 1) / 8;
						int x = (data_byte1) % 8;

						cells[y][x] = !cells[y][x];
						debug_cells();

						// Accendi il LED corrispondente al tasto premuto
						// Esempio: stesso numero di nota, velocity 0x7F
						// (massima luminosità, colore standard) Il valore
						// di velocity per i LED varia tra i modelli di
						// Launchpad. 0x0C (verde basso), 0x3C (verde
						// pieno), 0x0D (rosso basso), ecc. Per semplicità,
						// usiamo una velocity fissa (es. 0x7F o un colore
						// specifico)

						output_buffer[0] = (status_byte & 0xF0);
						// messaggio ricevuto)
						output_buffer[1] =
							note_number; // Stessa nota del tasto premuto
						output_buffer[2] =
							(cells[y][x]) ? 0x0E : 0x00; // Esempio: LED verde acceso (controlla il
								  // manuale del tuo Launchpad per i valori di
								  // velocity/colore)

						// printf("    Invio MIDI Out: %02X %02X %02X\n",
						// 	   output_buffer[0], output_buffer[1],
						// 	   output_buffer[2]);

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
					} else { // Tasto rilasciato (Note On con velocity 0 è
							 // spesso un Note Off)
							 // Spegni il LED corrispondente
							 // output_buffer[0] = (status_byte & 0xF0);
							 // output_buffer[1] = note_number; // Stessa nota
						// output_buffer[2] = 0x00; // Velocity 0 (LED spento)

						// // printf("    Invio MIDI Out (LED OFF): %02X %02X
						// // %02X\n", 	   output_buffer[0], output_buffer[1],
						// // 	   output_buffer[2]);

						// if ((err = snd_rawmidi_write(midi_out, output_buffer,
						// 							 3)) < 0) {
						// 	fprintf(stderr,
						// 			"Errore durante l'invio del messaggio MIDI "
						// 			"(LED OFF): %s\n",
						// 			snd_strerror(err));
						// }
					}
					i += 3; // Avanza al prossimo messaggio MIDI (assumendo
							// messaggi da 3 byte)
				} else if ((status_byte & 0xF0) == 0x80) { // Note Off (0x8n)
					unsigned char note_number = data_byte1;
					// unsigned char velocity = data_byte2; // Spesso 0x00 o
					// 0x40 per Note Off

					// printf("  -> Note Off: Nota=%02X (%d), Canale=%d\n",
					// 	   note_number, note_number, status_byte & 0x0F);

					// Spegni il LED corrispondente
					output_buffer[0] =
						0x90; // Usa Note On con velocity 0 per spegnere, per
							  // coerenza con Launchpad
					output_buffer[1] = note_number;
					output_buffer[2] = 0x00; // Velocity 0 (LED spento)

					// printf("    Invio MIDI Out (LED OFF): %02X %02X %02X\n",
					// 	   output_buffer[0], output_buffer[1],
					// 	   output_buffer[2]);

					if ((err = snd_rawmidi_write(midi_out, output_buffer, 3)) <
						0) {
						fprintf(stderr,
								"Errore durante l'invio del messaggio MIDI "
								"(LED OFF): %s\n",
								snd_strerror(err));
					}
					i += 3;
				}
				// Aggiungere qui il parsing per altri tipi di messaggi MIDI se
				// necessario
				else {
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
