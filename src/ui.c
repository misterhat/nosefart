#include <SDL2/SDL.h>
#include <libgen.h>
#include <stdio.h>

#include "config.h"
#include "kiss_sdl.h"
#include "nsf.h"
#include "nsfinfo.h"
#include "types.h"

/* main window size (widescreen) */
#define WIDTH (16 * 34)
#define HEIGHT (9 * 34)

/* UI element sizes */
#define BUTTON_WIDTH 62
#define COMBO_WIDTH 80
#define COLUMN_WIDTH (WIDTH / 2)
#define SELECTBUTTON_WIDTH 15

/* general UI padding */
#define PADDING 6

/* amount of NES voices */
#define CHANNELS 6

static nsf_t *nsf = 0;

static uint32 freq = 44100;
static uint16 bits = 8;

static int dataSize;
static int bufferSize;
static unsigned char *buffer = 0, *bufferPos = 0;

static int frames;
static int *plimit_frames = NULL;

static int channels_enabled[CHANNELS];

static kiss_array kiss_objects;
static kiss_window window;
static SDL_Renderer *renderer;
static kiss_selectbutton channel_buttons[CHANNELS];

void button_event(kiss_button *button, SDL_Event *e, int *draw, int *quit) {
    if (kiss_button_event(button, e, draw)) {
        *quit = 1;
    }
}

/* initialize sdl and kiss UI */
static void init_sdl() {
    kiss_array_new(&kiss_objects);

    renderer = kiss_init("Nosefart", &kiss_objects, WIDTH, HEIGHT);

    if (!renderer) {
        fprintf(stderr, "kiss_init (SDL_Init():): %s\n", SDL_GetError());
        exit(1);
    }

    kiss_window_new(
        &window,
        NULL,
        0,
        0,
        0,
        kiss_screen_width,
        kiss_screen_height
    );

    if (SDL_Init(SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init(): %s\n", SDL_GetError());
        exit(1);
    }

    SDL_AudioSpec wanted;

    int format;

    if (bits == 8) {
        format = AUDIO_U8;
    } else if (bits == 16) {
        format = AUDIO_S16;
    } else {
        fprintf(stderr, "Bad sample depth: %i\n", bits);
        exit(1);
    }

    wanted.freq = freq;
    wanted.format = format;
    wanted.channels = 1;
    wanted.silence = 0;
    wanted.samples = 1024;
    wanted.callback = NULL;

    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "SDL_OpenAudio(): %s\n", SDL_GetError());
        exit(1);
    }

    SDL_PauseAudio(0);
}

static void init_buffer() {
    dataSize = freq / nsf->playback_rate * (bits / 8);
    bufferSize = ((freq * bits) / 8) / 2;
    buffer = malloc((bufferSize / dataSize + 1) * dataSize);
    bufferPos = buffer;
    memset(buffer, 0, bufferSize);
}

/* update the UI buttons and nosefart instance for enabled/disabled channels */
static void sync_channels() {
    for (int i = 0; i < CHANNELS; i++) {
        channel_buttons[i].selected = channels_enabled[i];
    }
}

/* returns the number of frames to play */
static int get_time(char *filename, int track) {
    /* raw result, with intro, without intro */
    int result, wintro, wointro;

    result = time_info(filename, track);

    wintro = result / 0x1000;
    wointro = result % 0x1000;

    return wintro;
}

/* determine how long a track is */
void handle_auto_calc(char *filename, int track) {
    *plimit_frames = get_time(filename, track);
}

static int load_nsf_file(char *filename) {
    nsf_init();

    /* load up an NSF file */
    nsf = nsf_load(filename, 0, 0);

    if (!nsf) {
        fprintf(stderr, "Error opening \"%s\"\n", filename);
        exit(1);
    }
}

/* start track, display which it is, and what channels are enabled */
static void nsf_setupsong() {
    nsf_playtrack(nsf, nsf->current_song, freq, bits, 0);
    sync_channels();
}

static void show_info() {
    printf("NSF_NAME=%s\n", nsf->song_name);
    printf("NSF_ARTIST=%s\n", nsf->artist_name);
    printf("NSF_COPYRIGHT=%s\n", nsf->copyright);
    printf("NSF_TRACK_COUNT=%d\n", nsf->num_songs);
}

static void dump(char* filename, char *dumpname, int track) {
    memset(buffer, 0, bufferSize);

    int done = 0;
    frames = 0;
    bufferPos = buffer;

    FILE *wavFile = fopen(dumpname, "wb");

    fwrite("RIFF", 4, 1, wavFile);

    uint32 size = 0;
    fwrite(&size, sizeof(uint32), 1, wavFile);

    fwrite("WAVEfmt ", 8, 1, wavFile);

    uint32 headerSize = 16;
    fwrite(&headerSize, sizeof(uint32), 1, wavFile);

    uint16 type = 1;
    fwrite(&type, sizeof(uint16), 1, wavFile);

    uint16 channels = 1;
    fwrite(&channels, sizeof(uint16), 1, wavFile);

    fwrite(&freq, sizeof(uint32), 1, wavFile);

    uint32 bytesPerSecond = (freq * bits) / 8;
    fwrite(&bytesPerSecond, sizeof(uint32), 1, wavFile);

    uint16 blockAlign = (freq / 8);
    fwrite(&blockAlign, sizeof(uint16), 1, wavFile);

    fwrite(&bits, sizeof(uint16), 1, wavFile);

    fwrite("data", 4, 1, wavFile);
    fwrite(&size, sizeof(uint32), 1, wavFile);

    handle_auto_calc(filename, nsf->current_song);
    nsf_playtrack(nsf, nsf->current_song, freq, bits, 0);
    sync_channels();

    while (!done) {
        nsf_frame(nsf);
        frames++;
        apu_process(bufferPos, dataSize / (bits / 8));
        bufferPos += dataSize;

        if (bufferPos >= buffer + bufferSize) {
            fwrite(buffer, 1, bufferPos - buffer, wavFile);
            size += bufferPos - buffer;
            bufferPos = buffer;
        }

        if (frames >= 50 && frames >= *plimit_frames) {
            done = 1;
        }
    }

    fseek(wavFile, 40, SEEK_SET);
    fwrite(&size, sizeof(uint32), 1, wavFile);

    fseek(wavFile, 4, SEEK_SET);
    size += 36;
    fwrite(&size, sizeof(uint32), 1, wavFile);

    fclose(wavFile);
}

static void close_nsf_file(void) {
    nsf_free(&nsf);
    nsf = 0;
}

static void close_sdl(void) {
    SDL_Quit();
    free(buffer);
    buffer = 0;
    bufferSize = 0;
    bufferPos = 0;
}

static void show_help() {
    printf("Usage: %s [OPTIONS] filename\n", NAME);
    printf("Play an NSF (NES Sound Format) file.\n");
    printf("\nOptions:\n");
    printf("\t-h  \tHelp\n");
    printf("\t-v  \tVersion information\n");
    printf("\n\t-t x\tStart playing track x (default: 1)\n");
    printf("\t-s x\tPlay at x times the normal speed\n");
    printf("\t-f x\tUse x sampling rate (default: 44100)\n");
    printf("\t-B x\tUse sample size of x bits (default: 8)\n");
    printf("\t-l x\tLimit total playing time to x seconds (0 = unlimited)\n");
    printf("\t-r x\tLimit total playing time to x frames (0 = unlimited)\n");
    printf("\t-b x\tSkip the first x frames\n");
    printf("\t-i\tJust print file information and exit\n");
    printf("\t-x\tStart with channel x disabled (-123456)\n");
    printf("\t-o x\tOutput WAV files to directory x\n\n");
    printf("See https://github.com/misterhat/nosefart\n");

    exit(0);
}

static void show_version() {
    printf("%s -- NSF player for Linux\n", NAME);

    printf("Version " VERSION " ");
    printf("Compiled with GCC %i.%i on %s %s\n", __GNUC__, __GNUC_MINOR__,
           __DATE__, __TIME__);

    printf("\nNSF support by Matthew Conte. ");
    printf("Inspired by the MSP 0.50 source release by Sevy\nand Marp. ");
    printf("Ported by Neil Stevens. Some changes by Matthew Strait, with");
    printf(" more\nchanges made by Zorian Medwid.\n");

    exit(0);
}

int main(int argc, char** argv) {
    for (int i = 0; i < CHANNELS; i += 1) {
        channels_enabled[i] = 1;
    }

    char *filename;
    char *dump_wav_dir;
    int track = 1;
    int done = 0;
    int just_show_info = 0;
    int dump_wav = 0;
    int reps = 0;
    int limit_time = 0;
    int starting_frame = 0;
    int limited = 0;
    float speed_multiplier = 1;

    const char *opts = "123456hvit:f:B:s:l:r:b:o:";

    plimit_frames = (int *)malloc(sizeof(int));
    plimit_frames[0] = 0;

    while (!done) {
        char c;

        switch (c = getopt(argc, argv, opts)) {
        case EOF:
            done = 1;
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
            channels_enabled[(int)(c - '0' - 1)] = 0;
            break;
        case 'v':
            show_version();
            break;
        case 't':
            track = strtol(optarg, 0, 10);
            break;
        case 'f':
            freq = strtol(optarg, 0, 10);
            break;
        case 'B':
            bits = strtol(optarg, 0, 10);
            break;
        case 's':
            speed_multiplier = atof(optarg);
            break;
        case 'i':
            just_show_info = 1;
            break;
        case 'l':
            limit_time = atoi(optarg);
            limited = 1;
            break;
        case 'r':
            *plimit_frames = atoi(optarg);
            limited = 1;
            break;
        case 'b':
            starting_frame = atoi(optarg);
            break;
        case 'o':
            dump_wav = 1;
            dump_wav_dir = optarg;
            break;
        case 'h':
        case ':':
        case '?':
        default:
            show_help();
            break;
        }
    }

    /* filename comes after all other options */
    if (argc <= optind) {
        show_help();
    }

    filename = malloc(strlen(argv[optind]) + 1);
    strcpy(filename, argv[optind]);

    if (limit_time == 0) {
        handle_auto_calc(filename, track);
    }

    load_nsf_file(filename);

    nsf->playback_rate *= speed_multiplier;

    if (just_show_info) {
        show_info();
        return 0;
    }

    init_buffer();

    if (limit_time != 0) {
        *plimit_frames = limit_time * nsf->playback_rate;
    }

    if (dump_wav) {
        mkdir(dump_wav_dir, 0777);

        for (int i = track; i < nsf->num_songs; i++) {
            nsf->current_song = i;

            /* 3 digits, WAV extension, slash, dot and NULL */
            char* dumpname = malloc(strlen(dump_wav_dir) + 9);
            sprintf(dumpname, "%s/%d.wav", dump_wav_dir, nsf->current_song);

            dump(filename, dumpname, i);
            free(dumpname);
        }

        return 0;
    }

    init_sdl();

    int draw = 1;
    int quit = 0;

    /* header widgets (filename and browse) */

    int y = PADDING;
    int x = PADDING;

    char* base_name = basename(filename);

    if (strlen(base_name) > 49) {
        base_name[49] = '.';
        base_name[50] = '.';
        base_name[51] = '.';
        base_name[52] = '\0';
    }

    kiss_label header_label;
    kiss_label_new(&header_label, &window, base_name, x, y);

    x = WIDTH - BUTTON_WIDTH - PADDING;

    kiss_button button;
    kiss_button_new(&button, &window, "Browse", x, y);

    x = PADDING;
    y += kiss_textfont.fontheight + PADDING * 2;

    /* track number selection */

    kiss_label track_label;
    kiss_label_new(&track_label, &window, "Track:", x, y);

    x += kiss_textwidth(kiss_textfont, "Track:", NULL) + PADDING;

    kiss_array track_names;
    kiss_array_new(&track_names);

    kiss_array_appendstring(&track_names, 0, "1", NULL);
    kiss_array_appendstring(&track_names, 1, "2", NULL);
    kiss_array_appendstring(&track_names, 2, "3", NULL);
    kiss_array_appendstring(&track_names, 3, "4", NULL);
    kiss_array_appendstring(&track_names, 4, "5", NULL);

    kiss_combobox track_combo;

    kiss_combobox_new(
        &track_combo,
        &window,
        "None",
        &track_names,
        x,
        y - 4,
        COMBO_WIDTH,
        100
    );

    x = PADDING;
    y += kiss_textfont.fontheight + PADDING * 3;

    /* song information */

    kiss_label song_info_label;

    char song_info_text[KISS_MAX_LENGTH];

    sprintf(
        song_info_text,
        "Name:\n  %s\n\nArtist:\n  %s\n\nCopyright:\n  %s",
        nsf->song_name,
        nsf->artist_name,
        nsf->copyright
    );

    kiss_label_new(
        &song_info_label,
        &window,
        song_info_text,
        x,
        y
    );

    y += kiss_textfont.fontheight * 9 + PADDING * 4;

    /* channel widgets */

    y = kiss_textfont.fontheight + PADDING * 2;
    x = COLUMN_WIDTH;

    kiss_label channels_label;
    kiss_label_new(&channels_label, &window, "Channels:", x, y);

    char* channel_names[CHANNELS] = {
        "  Pulse 1",
        "  Pulse 2",
        "  Triangle",
        "  Noise",
        "  Sample",
        "  External"
    };

    kiss_label channel_labels[CHANNELS];

    y += kiss_textfont.fontheight + PADDING;

    for (int i = 0; i < CHANNELS; i++) {
        kiss_label_new(&channel_labels[i], &window, channel_names[i], x, y);

        int width = kiss_textwidth(kiss_textfont, channel_names[i], NULL);

        kiss_selectbutton_new(
            &channel_buttons[i],
            &window,
            x + width + PADDING * 2,
            y + 2
        );

        y += kiss_textfont.fontheight + PADDING;
    }

    sync_channels();

    /* playback widgets */

    x = WIDTH - 100;
    y += PADDING * 8;

    kiss_label replay_label;
    kiss_label_new(&replay_label, &window, "Replay:", x, y);

    x += kiss_textwidth(kiss_textfont, "Replay:", NULL) + PADDING;

    kiss_selectbutton replay_button;
    kiss_selectbutton_new(&replay_button, &window, x, y + 2);

    x = PADDING;
    y += (kiss_textfont.fontheight) + PADDING * 2;

    kiss_button previous_button;
    kiss_button_new(&previous_button, &window, "Previous", x, y);

    x += BUTTON_WIDTH + PADDING;

    kiss_button play_button;
    kiss_button_new(&play_button, &window, "Play", x, y);

    x += BUTTON_WIDTH + PADDING;

    kiss_button next_button;
    kiss_button_new(&next_button, &window, "Next", x, y);

    x += BUTTON_WIDTH + PADDING;
    y += PADDING;

    kiss_hscrollbar track_seek;
    kiss_hscrollbar_new(&track_seek, &window, x, y - 2, WIDTH - x - PADDING);

    window.visible = 1;

    SDL_Event e;

    while (!quit) {
        //play_tick();

        SDL_Delay(16);

        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = 1;
            }

            kiss_window_event(&window, &e, &draw);
            button_event(&button, &e, &draw, &quit);

            for (int i = 0; i < CHANNELS; i++) {
                kiss_selectbutton_event(&channel_buttons[i], &e, &draw);
            }

            kiss_combobox_event(&track_combo, &e, &draw);
            kiss_selectbutton_event(&replay_button, &e, &draw);

            kiss_button_event(&previous_button, &e, &draw);
            kiss_button_event(&play_button, &e, &draw);
            kiss_button_event(&next_button, &e, &draw);
            kiss_hscrollbar_event(&track_seek, &e, &draw);
        }

        if (!draw) {
            continue;
        }

        SDL_RenderClear(renderer);

        kiss_window_draw(&window, renderer);

        kiss_label_draw(&header_label, renderer);
        kiss_button_draw(&button, renderer);

        kiss_label_draw(&song_info_label, renderer);

        kiss_label_draw(&channels_label, renderer);

        for (int i = 0; i < CHANNELS; i++) {
            kiss_label_draw(&channel_labels[i], renderer);
            kiss_selectbutton_draw(&channel_buttons[i], renderer);
        }

        kiss_label_draw(&track_label, renderer);
        kiss_label_draw(&replay_label, renderer);
        kiss_selectbutton_draw(&replay_button, renderer);

        kiss_button_draw(&previous_button, renderer);
        kiss_button_draw(&play_button, renderer);
        kiss_button_draw(&next_button, renderer);
        kiss_hscrollbar_draw(&track_seek, renderer);

        /* draw on top */
        kiss_combobox_draw(&track_combo, renderer);

        SDL_RenderPresent(renderer);
        draw = 0;
    }

    close_nsf_file();
    close_sdl();
    kiss_clean(&kiss_objects);

    return 0;
}
