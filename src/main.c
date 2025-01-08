#include <stdio.h>
#include <stdint.h>
#include <SDL3/SDL.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define FRAME_RATE 120

#define DESIRED_FRAME_TIME (1.0 / FRAME_RATE)

// These goobers make sure that function calls passed to the macro only get called once
#define MIN(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) < (_Y) ? (_X) : (_Y);\
                   })
#define MAX(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) > (_Y) ? (_X) : (_Y);\
                   })

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} Color;

typedef struct {
    float x;
    float y;
    float speed;
    float radius;
    float angle;
} Player;

typedef struct {
    bool quit;

    // time in seconds
    double last_frame; 
    double delta_time;
} EngineState;

#define MAP_WIDTH 8
#define MAP_HEIGHT 8
int map[MAP_WIDTH * MAP_HEIGHT] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 1, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
};

// Global state
EngineState e_state = {
    .quit = false,
    .last_frame = 0.0,
    .delta_time = 0.0f,
};

Player player = {
    .x = 2.0f,
    .y = 2.0f,
    .speed = 1.0f,
    .radius = 0.2f,
    .angle = 270.0f,
};

void init_sdl(SDL_Renderer **renderer, SDL_Window **window, int width, int height) {
    if(!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("ERROR: Failed to initialize SDL\n%s", SDL_GetError());
        SDL_Quit();
    }
    if (!SDL_CreateWindowAndRenderer("Racaster", width, height, 0, window, renderer)) {
        SDL_Log("ERROR: Failed to create window\n%s", SDL_GetError());
        SDL_Quit();
    }

}

void handle_events() {
    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) e_state.quit = true;
    }

}

void move_player() {
    const bool *keys = SDL_GetKeyboardState(NULL);
    int cell_x;
    int cell_y;

    // vertical
    if (keys[SDL_SCANCODE_W]) {
        player.y -= player.speed * e_state.delta_time;
        player.y = MAX(player.y, 0);
    }
    if (keys[SDL_SCANCODE_S]) {
        player.y += player.speed * e_state.delta_time;
        player.y = MIN(player.y, MAP_HEIGHT);
    }
    // collision
    cell_y = (int)(player.y - player.radius);
    if (map[cell_y * MAP_WIDTH + (int)player.x] != 0)
        player.y = (cell_y + 1) + player.radius; // need to add one since cell coords are top left
    cell_y = (int)(player.y + player.radius);
    if (map[cell_y * MAP_WIDTH + (int)player.x] != 0)
        player.y = cell_y - player.radius;

    // horrizontal
    if (keys[SDL_SCANCODE_D]) {
        player.x += player.speed * e_state.delta_time;
        player.x = MIN(player.x, MAP_WIDTH);
    }
    if (keys[SDL_SCANCODE_A]) {
        player.x -= player.speed * e_state.delta_time;
        player.x = MAX(player.x, 0);
    }
    // collision
    cell_x = (int)(player.x + player.radius);
    if (map[(int)player.y * MAP_WIDTH + cell_x] != 0)
        player.x = cell_x - player.radius;
    cell_x = (int)(player.x - player.radius);
    if (map[(int)player.y * MAP_WIDTH + cell_x] != 0)
        player.x = (cell_x + 1) + player.radius;

}

// https://gist.github.com/Gumichan01/332c26f6197a432db91cc4327fcabb1c
int render_fill_circle(SDL_Renderer *renderer, int x, int y, int radius) {
    int offsetx, offsety, d;
    int status;

    offsetx = 0;
    offsety = radius;
    d = radius -1;
    status = 0;
    while (offsety >= offsetx) {
        status += SDL_RenderLine(renderer, x - offsety, y + offsetx,
                                     x + offsety, y + offsetx);
        status += SDL_RenderLine(renderer, x - offsetx, y + offsety,
                                 x + offsetx, y + offsety);
        status += SDL_RenderLine(renderer, x - offsetx, y - offsety,
                                 x + offsetx, y - offsety);
        status += SDL_RenderLine(renderer, x - offsety, y - offsetx,
                                     x + offsety, y - offsetx);

        if (status < 0) {
            status = -1;
            break;
        }
        if (d >= 2*offsetx) {
            d -= 2*offsetx + 1;
            offsetx +=1;
        }
        else if (d < 2 * (radius - offsety)) {
            d += 2 * offsety - 1;
            offsety -= 1;
        }
        else {
            d += 2 * (offsety - offsetx - 1);
            offsety -= 1;
            offsetx += 1;
        }
    }
    return status;
}

void draw_level_map(SDL_Renderer *renderer) {
    // Clear Black
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    size_t col_size = WINDOW_WIDTH / MAP_WIDTH;
    size_t row_size = WINDOW_HEIGHT / MAP_HEIGHT;

    // White grid
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (size_t row = 0; row < MAP_HEIGHT; row++) {
        for (size_t col = 0; col < MAP_WIDTH; col++) {
            SDL_FRect rect = {
                .x = col * col_size,
                .y = row * row_size,
                .w = col_size,
                .h = row_size,
            };
            if (map[row * MAP_WIDTH + col] == 1) {
               SDL_SetRenderDrawColor(renderer, 0, 0, 155, 255);
               SDL_RenderFillRect(renderer, &rect);
               SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            } else {
               SDL_RenderRect(renderer, &rect);
            }
        }
    }

    // Player
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    render_fill_circle(renderer, col_size * player.x, row_size * player.y, col_size * player.radius);
}

int main() {
    SDL_Window *window;
    SDL_Renderer *renderer;
    init_sdl(&renderer, &window, WINDOW_WIDTH, WINDOW_HEIGHT);

    while(!e_state.quit) {
        // update time
        double time = SDL_GetTicks() / 1000.0;
        double prev_frame_time = time - e_state.last_frame;
        if (prev_frame_time < DESIRED_FRAME_TIME) {
            SDL_Delay(DESIRED_FRAME_TIME - prev_frame_time);
        }
        time += DESIRED_FRAME_TIME - prev_frame_time;
        e_state.delta_time = time - e_state.last_frame;
        e_state.last_frame = time;

        printf("%f, %f\n", player.x, player.y);

        // inputs
        handle_events();
        move_player();

        // render
        draw_level_map(renderer);

        SDL_RenderPresent(renderer);
    }


    return 0;
}
