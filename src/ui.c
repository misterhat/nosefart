#include <stdio.h>

#include "kiss_sdl/kiss_sdl.h"

#define WIDTH 544
#define HEIGHT 306

#define BUTTON_WIDTH 62
#define COMBO_WIDTH 80
#define COLUMN_WIDTH (WIDTH / 2)
#define SELECTBUTTON_WIDTH 15

#define PADDING 6

#define CHANNELS 6

void button_event(kiss_button *button, SDL_Event *e, int *draw, int *quit) {
	if (kiss_button_event(button, e, draw)) {
        *quit = 1;
    }
}

int main(int argc, char** argv) {
    int draw = 1;
    int quit = 0;

    kiss_array objects;
    kiss_array_new(&objects);

    SDL_Renderer *renderer = kiss_init("Nosefart", &objects, WIDTH, HEIGHT);

    if (!renderer) {
        sprintf("kiss_init (SDL_Init): %s\n", SDL_GetError());
        return 1;
    }

    kiss_window window;

    kiss_window_new(
        &window,
        NULL,
        0,
        0,
        0,
        kiss_screen_width,
        kiss_screen_height
    );

    /* header widgets */

    char* nsf_path = "/home/zorian/test.nsf";

    int y = PADDING;
    int x = PADDING;

    kiss_label header_label;
    kiss_label_new(&header_label, &window, nsf_path, x, y);

    kiss_button button;

    x = WIDTH - BUTTON_WIDTH - PADDING;

    kiss_button_new(
        &button,
        &window,
        "Browse",
        x,
        y
    );

    x = PADDING;
    y += kiss_textfont.fontheight + PADDING * 2;

    kiss_label track_label;

    kiss_label_new(
        &track_label,
        &window,
        "Track:",
        x,
        y
    );

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

    kiss_label name_label;

    kiss_label_new(
        &name_label,
        &window,
        "Name:\n  Super Mario Bros. 3",
        x,
        y
    );

    y += kiss_textfont.fontheight * 3 + PADDING;

    kiss_label artist_label;

    kiss_label_new(
        &artist_label,
        &window,
        "Artist:\n  Koji Kondo",
        x,
        y
    );

    y += kiss_textfont.fontheight * 3 + PADDING;

    kiss_label copyright_label;

    kiss_label_new(
        &copyright_label,
        &window,
        "Copyright:\n  1988 Nintendo",
        x,
        y
    );

    y += kiss_textfont.fontheight * 3 + PADDING * 2;

    /* channel widgets */

    y = kiss_textfont.fontheight + PADDING * 2;
    x = COLUMN_WIDTH;

    kiss_label channels_label;
    kiss_label_new(&channels_label, &window, "Channels:", x, y);

    char* channels[CHANNELS] = {
        "  Pulse 1",
        "  Pulse 2",
        "  Triangle",
        "  Noise",
        "  Sample",
        "  External"
    };

    kiss_label channel_labels[CHANNELS];
    kiss_selectbutton channel_buttons[CHANNELS];

    y += kiss_textfont.fontheight + PADDING;

    for (int i = 0; i < CHANNELS; i++) {
        kiss_label_new(&channel_labels[i], &window, channels[i], x, y);

        int width = kiss_textwidth(kiss_textfont, channels[i], NULL);

        kiss_selectbutton_new(
            &channel_buttons[i],
            &window,
            x + width + PADDING * 2,
            y + 2
        );

        channel_buttons[i].selected = 1;

        y += kiss_textfont.fontheight + PADDING;
    }

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
        SDL_Delay(10);

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

        kiss_label_draw(&name_label, renderer);
        kiss_label_draw(&artist_label, renderer);
        kiss_label_draw(&copyright_label, renderer);

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

    kiss_clean(&objects);

    return 0;
}
