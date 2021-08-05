/* Modified by Matthew Strait Oct-Nov 2002 */
/* Note that this runs in FreeBSD as well as Linux (and may well run in other
UNIX systems */

#include <SDL2/SDL.h>

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"
#include "nsf.h"
#include "types.h"

/* for auto-time calculation.  We ignore most of the stuff in there, but I
just including this is easier than trying to rip out just the relevant bits
and nothing else. */
#include <nsfinfo.h>

#ifndef bool
#define bool boolean
#endif

#ifndef true
#define true TRUE
#endif

/* This saves the trouble of passing our ONE nsf around the stack all day */
static nsf_t *nsf = 0;

/* thank you, nsf_playtrack, for making me pass freq and bits to you */
static uint32 freq = 44100;
static uint16 bits = 8;

/* sound */
static int dataSize;
static int bufferSize;
static unsigned char *buffer = 0, *bufferPos = 0;

static int *plimit_frames = NULL;

static int frames; /* I like global variables too much */

static struct termios oldterm;

/* whether the channels are enabled or not.  Moved out here by Matthew Strait
so that it could be displayed */
static bool enabled[6] = {true, true, true, true, true, true};

/* takes the number of repetitions desired and returns the number of frames
to play */
static int get_time(int repetitions, char *filename, int track) {
    /* raw result, with intro, without intro */
    int result, wintro, wointro;

    result = time_info(filename, track);

    wintro = result / 0x1000;
    wointro = result % 0x1000;

    return wintro + (repetitions - 1) * wointro;
}

void handle_auto_calc(char *filename, int track, int reps) {
    *plimit_frames = get_time(reps, filename, track);
}

static void init_sdl(void) {
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
        printf("Bad sample depth: %i\n", bits);
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

/* close what we've opened */
static void close_sdl(void) {
    SDL_Quit();
    free(buffer);
    buffer = 0;
    bufferSize = 0;
    bufferPos = 0;
}

static void show_help(void) {
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
    printf("\t-a x\tCalculate song length and play x repetitions (0 = intro "
           "only)\n");
    printf("\t-i\tJust print file information and exit\n");
    printf("\t-x\tStart with channel x disabled (-123456)\n");
    printf("\t-o x\tOutput WAV files to directory x\n\n");
    printf("\nPlease send bug reports to quadong@users.sf.net\n");

    exit(0);
}

static void show_warranty(void) {
    printf("\nThis software comes with absolutely no warranty. ");
    printf("You may redistribute this\nprogram under the terms of the GPL. ");
    printf("(See the file COPYING in the source tree.)\n\n");
}

static void show_info(void) {
    printf("%s -- NSF player for Linux\n", NAME);
    printf("Version " VERSION " ");
    printf("Compiled with GCC %i.%i on %s %s\n", __GNUC__, __GNUC_MINOR__,
           __DATE__, __TIME__);
    printf("\nNSF support by Matthew Conte. ");
    printf("Inspired by the MSP 0.50 source release by Sevy\nand Marp. ");
    printf("Ported by Neil Stevens.  Some changes by Matthew Strait.\n");

    exit(0);
}

static void printsonginfo(int current_frame, int total_frames, int limited) {
    /*Why not printf directly?  Our termios hijinks for input kills the output*/
    char *ui = (char *)malloc(255);

    snprintf(
        ui, 254,
        total_frames != 0
            ? "Playing track %d/%d, channels %c%c%c%c%c%c, %d/%d sec, %d/%d "
              "frames\r"
            : (limited ? "Playing track %d/%d, channels %c%c%c%c%c%c, %d/? "
                         "sec, %11$d/? frames (Working...)\r"
                       : "Playing track %d/%d, channels %c%c%c%c%c%c, %d "
                         "sec, %11$d frames\r"),
        nsf->current_song, nsf->num_songs, enabled[0] ? '1' : '-',
        enabled[1] ? '2' : '-', enabled[2] ? '3' : '-', enabled[3] ? '4' : '-',
        enabled[4] ? '5' : '-', enabled[5] ? '6' : '-',
        abs((int)((float)(current_frame + nsf->playback_rate) /
                  (float)nsf->playback_rate) -
            1), /* the sound gets buffered a lot... */
        abs((int)((float)(total_frames + nsf->playback_rate) /
                  (float)nsf->playback_rate) -
            1), /* this is something of an estimate */
        current_frame, total_frames);

    if (!(current_frame % 10)) {
        char blank[82];
        memset(blank, ' ', 80);
        blank[80] = '\r';
        blank[81] = '\0';
        write(STDOUT_FILENO, (void *)blank, 81);
    }

    write(STDOUT_FILENO, (void *)ui, strlen(ui));
    free(ui);
}

static void sync_channels(void) {
    /* this is going to get run when a track starts and all channels start out
       enabled, so just turn off the right ones */
    for (int channel = 0; channel < 6; channel++) {
        if (!enabled[channel]) {
            nsf_setchan(nsf, channel, enabled[channel]);
        }
    }
}

/* start track, display which it is, and what channels are enabled */
static void nsf_setupsong() {
    printsonginfo(0, 0, 0);
    nsf_playtrack(nsf, nsf->current_song, freq, bits, 0);
    sync_channels();

    return;
}

/* display info about an NSF file */
static void nsf_displayinfo(void) {
    printf("Keys:\n");
    printf("q:\tquit\n");
    printf("x:\tplay the next track\n");
    printf("z:\tplay the previous track\n");
    printf("return:\trestart track\n");
    printf("1-6:\ttoggle channels\n\n");

    printf("NSF Information:\n");
    printf("Song name:       %s\n", nsf->song_name);
    printf("Artist:          %s\n", nsf->artist_name);
    printf("Copyright:       %s\n", nsf->copyright);
    printf("Number of Songs: %d\n\n", nsf->num_songs);

    fflush(stdout);
}

/* handle keypresses */
static int nsf_handlekey(char ch, int doautocalc, char *filename, int reps) {
    ch = tolower(ch);
    switch (ch) {
    case 'q':
    case 27: /* escape */
        return 1;
    case 'x':
        if (nsf->current_song == nsf->num_songs) {
            nsf->current_song = 1;
        } else {
            nsf->current_song++;
        }

        nsf_setupsong();
        frames = 0;

        if (doautocalc) {
            handle_auto_calc(filename, nsf->current_song, reps);
        }

        break;
    case 'z':
        if (nsf->current_song == 1) {
            nsf->current_song = nsf->num_songs;
        } else {
            nsf->current_song--;
        }

        nsf_setupsong();
        frames = 0;

        if (doautocalc) {
            handle_auto_calc(filename, nsf->current_song, reps);
        }
        break;
    case '\n':
        nsf_playtrack(nsf, nsf->current_song, freq, bits, 0);
        sync_channels();
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
        enabled[ch - '1'] = !enabled[ch - '1'];
        nsf_setchan(nsf, ch - '1', enabled[ch - '1']);
        break;
    }

    return 0;
}

/* load the nsf, so we know how to set up the audio */
static int load_nsf_file(char *filename) {
    nsf_init();

    /* load up an NSF file */
    nsf = nsf_load(filename, 0, 0);

    if (!nsf) {
        printf("Error opening \"%s\"\n", filename);
        exit(1);
    }
}

static void setup_term() {
    struct termios term;

    tcgetattr(STDIN_FILENO, &term);
    oldterm = term;
    term.c_lflag &= ~ICANON;
    term.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
}

static void play(char *filename, int track, int doautocalc, int reps,
                 int starting_frame, int limited) {
    int done = 0;
    frames = 0;

    /* determine which track to play */
    if (track > nsf->num_songs || track < 1) {
        nsf->current_song = nsf->start_song;

        fprintf(stderr, "track %d out of range, playing track %d\n", track,
                nsf->current_song);
    } else {
        nsf->current_song = track;
    }

    /* display file information */
    nsf_displayinfo();
    nsf_setupsong();

    setup_term();

    while (!done) {
        char ch;

        nsf_frame(nsf);
        frames++;

        printsonginfo(frames, *plimit_frames, limited);

        /* don't waste time if skipping frames (this check speeds it up a lot)
         */
        if (frames >= starting_frame) {
            apu_process(bufferPos, dataSize / (bits / 8));
        }

        bufferPos += dataSize;

        if (bufferPos >= buffer + bufferSize) {
            if (frames >= starting_frame) {
                SDL_QueueAudio(1, buffer, bufferPos - buffer);

                float bytesPerSecond = (freq * bits) / 8;

                SDL_Delay(
                    ((int)(SDL_GetQueuedAudioSize(1) / bytesPerSecond * 1000)) /
                    2);
            }

            bufferPos = buffer;
        }

        if (*plimit_frames != 0 && frames >= *plimit_frames) {
            done = 1;
        }

        if (fread((void *)&ch, 1, 1, stdin) == 1) {
            done = nsf_handlekey(ch, doautocalc, filename, reps);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldterm);
    fprintf(stderr, "\n");
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

    handle_auto_calc(filename, nsf->current_song, 1);
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

/* free what we've allocated */
static void close_nsf_file(void) {
    nsf_free(&nsf);
    nsf = 0;
}

int main(int argc, char **argv) {
    char *filename;
    char *dumpwavdir;
    int track = 1;
    int done = 0;
    int justdisplayinfo = 0;
    int dumpwav = 0;
    int doautocalc = 0;
    int reps = 0, limit_time = 0, starting_frame = 0;
    int limited = 0;
    float speed_multiplier = 1;

    const char *opts = "123456hvit:f:B:s:l:r:b:a:o:";

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
            enabled[(int)(c - '0' - 1)] = 0;
            break;
        case 'v':
            show_info();
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
            justdisplayinfo = 1;
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
        case 'a':
            doautocalc = 1;
            limited = 1;
            reps = atoi(optarg);
            break;
        case 'o':
            dumpwav = 1;
            dumpwavdir = optarg;
            break;
        case 'h':
        case ':':
        case '?':
        default:
            show_help();
            break;
        }
    }

    show_warranty();

    /* filename comes after all other options */
    if (argc <= optind) {
        show_help();
    }

    filename = malloc(strlen(argv[optind]) + 1);
    strcpy(filename, argv[optind]);

    if (doautocalc) {
        printf("Using song length calculation. Note that this isn't perfectly "
               "accurate.\n\n");

        handle_auto_calc(filename, track, reps);
    }

    load_nsf_file(filename);

    nsf->playback_rate *= speed_multiplier;

    if (justdisplayinfo) {
        nsf_displayinfo();
    } else if (dumpwav) {
        init_buffer();

        mkdir(dumpwavdir, 0777);

        for (int i = track; i < nsf->num_songs; i++) {
            nsf->current_song = i;

            // 3 digits, WAV extension, slash, dot and NULL
            char* dumpname = malloc(strlen(dumpwavdir) + 9);
            sprintf(dumpname, "%s/%d.wav", dumpwavdir, nsf->current_song);

            dump(filename, dumpname, i);

            free(dumpname);
        }
    } else {
        init_buffer();
        init_sdl();

        if (limit_time != 0) {
            *plimit_frames = limit_time * nsf->playback_rate;
        }

        play(filename, track, doautocalc, reps, starting_frame, limited);
    }

    close_nsf_file();

    if (!justdisplayinfo && !dumpwav) {
        close_sdl();
    }

    return 0;
}
