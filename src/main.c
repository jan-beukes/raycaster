#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <SDL3/SDL.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define FRAME_RATE 1000

#define DESIRED_FRAME_TIME (1.0 / FRAME_RATE)
#define MOUSE_SENS 70.0f

// These goobers make sure that function calls passed to the macro only get called once
#define MIN(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) < (_Y) ? (_X) : (_Y);\
                   })
#define MAX(X, Y) ({ __typeof__(X) _X = X;\
                    __typeof__(Y) _Y = Y;\
                   (_X) > (_Y) ? (_X) : (_Y);\
                   })

#define PANIC(fmt, ...) ({ fprintf(stderr, fmt, ##__VA_ARGS__); exit(1); })

#define DEG2RAD (SDL_PI_F / 180.0f)
#define RAD2DEG (180.0f / SDL_PI_F)

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
    float fov;
} Player;

typedef struct {
    bool quit;
    bool map_mode;

    // time in seconds
    double last_frame; 
    double delta_time;

    // Mouse
    float mouse_x_pos;
    float mouse_y_pos;
    float mouse_xrel;
    float mouse_yrel;
} EngineState;

#define MAP_WIDTH 8
#define MAP_HEIGHT 8

#define MAP_X_SCALE ((float)WINDOW_WIDTH / MAP_WIDTH)
#define MAP_Y_SCALE ((float)WINDOW_HEIGHT / MAP_HEIGHT)
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
    .map_mode = true,
    .last_frame = 0.0,
    .delta_time = 0.0f,
};

Player player = {
    .x = 2.0f,
    .y = 2.0f,
    .speed = 1.0f,
    .radius = 0.2f,
    .angle = 0.0f,
    .fov = 45.0f,
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
    
    SDL_SetWindowRelativeMouseMode(*window, true);

}

void handle_events() {
    e_state.mouse_xrel = 0.0f;
    e_state.mouse_yrel = 0.0f;

    SDL_Event e;
    while(SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) e_state.quit = true;
        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            e_state.mouse_xrel = e.motion.xrel;
            e_state.mouse_yrel = e.motion.yrel;
            e_state.mouse_x_pos = e.motion.x;
            e_state.mouse_y_pos = e.motion.y;
        }
    }

}

void handle_player_input() {
    //---Keyboard Input---
    const bool *keys = SDL_GetKeyboardState(NULL);
    int cell_x;
    int cell_y;

    float dx = 0, dy = 0;
    if (keys[SDL_SCANCODE_W]) {
        dx += SDL_cos(player.angle * DEG2RAD);
        dy += SDL_sin(player.angle * DEG2RAD);
    }
    if (keys[SDL_SCANCODE_S]) {
        dx += -SDL_cos(player.angle * DEG2RAD);
        dy += -SDL_sin(player.angle * DEG2RAD);
    }
    // horrizontal
    if (keys[SDL_SCANCODE_D]) {
        float angle = player.angle + 90.0f;
        dx += SDL_cos(angle * DEG2RAD);
        dy += SDL_sin(angle * DEG2RAD);
    }
    if (keys[SDL_SCANCODE_A]) {
        float angle = player.angle - 90.0f;
        dx += SDL_cos(angle * DEG2RAD);
        dy += SDL_sin(angle * DEG2RAD);
    }
    dx = MIN(dx, 1.0f);
    dy = MIN(dy, 1.0f);
    player.y += dy * player.speed * e_state.delta_time;
    player.x += dx * player.speed * e_state.delta_time;
    player.x = MIN(MAP_WIDTH, MAX(player.x, 0));
    player.y = MIN(MAP_HEIGHT, MAX(player.y, 0));

    //--Collision---
    cell_y = (int)(player.y - player.radius);
    if (map[cell_y * MAP_WIDTH + (int)player.x] != 0)
        player.y = (cell_y + 1) + player.radius; // need to add one since cell coords are top left

    cell_y = (int)(player.y + player.radius);
    if (map[cell_y * MAP_WIDTH + (int)player.x] != 0)
        player.y = cell_y - player.radius;

    cell_x = (int)(player.x + player.radius);
    if (map[(int)player.y * MAP_WIDTH + cell_x] != 0)
        player.x = cell_x - player.radius;

    cell_x = (int)(player.x - player.radius);
    if (map[(int)player.y * MAP_WIDTH + cell_x] != 0)
        player.x = (cell_x + 1) + player.radius;

    //---Mouse Input---
    float mouse_delta = e_state.mouse_xrel;
    player.angle += MOUSE_SENS * mouse_delta * e_state.delta_time;
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

#define RAY_STEP 0.01f
void cast_ray(float x_start, float y_start, float angle, float *x_end, float *y_end) {
    assert(x_start > 0 && x_start < MAP_WIDTH && y_start > 0 && y_start < MAP_HEIGHT);

    // don't cast too far
    const float max_length = SDL_sqrtf(MAP_WIDTH*MAP_WIDTH + MAP_HEIGHT*MAP_HEIGHT);
    const float x_step = RAY_STEP * SDL_cos(angle * DEG2RAD);
    const float y_step = RAY_STEP * SDL_sin(angle * DEG2RAD);
    const int max_steps = max_length / RAY_STEP;
    for (int i = 0; i < max_steps; i++) {
        float curr_x = player.x + i * x_step;
        float curr_y = player.y + i * y_step;

        // in a wall
        if (map[(int)curr_y*MAP_WIDTH + (int)curr_x] != 0) {
            bool horizontal = map[(int)curr_y*MAP_WIDTH + (int)(curr_x - x_step)] == 0;
            bool vertical = map[(int)(curr_y - y_step)*MAP_HEIGHT + (int)curr_x] == 0;
            if (horizontal) {
                *x_end = x_step > 0 ? (int)curr_x : (int)(curr_x + 1);
                *y_end = curr_y;
                return;
            } else if (vertical) {
                *y_end = y_step > 0 ? (int)curr_y : (int)(curr_y + 1);
                *x_end = curr_x;
                return;
            }
            // hit a corner
            *x_end = x_step > 0 ? (int)curr_x : (int)(curr_x + 1);
            *y_end = y_step > 0 ? (int)curr_y : (int)(curr_y + 1);
            return;
        }
    }
}

// 2d map view
void draw_level_map(SDL_Renderer *renderer) {
    // Clear Black
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // White grid
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (size_t row = 0; row < MAP_HEIGHT; row++) {
        for (size_t col = 0; col < MAP_WIDTH; col++) {
            SDL_FRect rect = {
                .x = col * MAP_X_SCALE,
                .y = row * MAP_Y_SCALE,
                .w = MAP_X_SCALE,
                .h = MAP_Y_SCALE,
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
    render_fill_circle(renderer, MAP_X_SCALE * player.x, MAP_Y_SCALE * player.y, MAP_X_SCALE * player.radius);

    // Rays
    float angle_start = player.angle - player.fov / 2.0f;
    float angle_end = player.angle + player.fov / 2.0f;
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    for (float angle = angle_start; angle <= angle_end; angle += 1.0f) {
        float ray_x, ray_y;
        cast_ray(player.x, player.y, angle, &ray_x, &ray_y);
        SDL_RenderLine(renderer, MAP_X_SCALE * player.x, MAP_Y_SCALE * player.y,
                       MAP_X_SCALE * ray_x, MAP_Y_SCALE * ray_y);
    }

}


int main() {
    SDL_Window *window;
    SDL_Renderer *renderer;
    init_sdl(&renderer, &window, WINDOW_WIDTH, WINDOW_HEIGHT);

    while(!e_state.quit) {
        // update time
        double time = SDL_GetTicks() / 1000.0f;
        if (time - e_state.last_frame < DESIRED_FRAME_TIME) {
            double delay = 1000 * (DESIRED_FRAME_TIME - (time - e_state.last_frame));
            SDL_Delay(delay);
        }
        time = SDL_GetTicks() / 1000.0f;
        e_state.delta_time = time - e_state.last_frame;
        e_state.last_frame = time;
        char buf[32];
        sprintf(buf, "%f", e_state.delta_time);
        SDL_SetWindowTitle(window, buf);

        // inputs
        handle_events();
        handle_player_input();

        // render
        draw_level_map(renderer);

        SDL_RenderPresent(renderer);
    }

    return 0;
}
