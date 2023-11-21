#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"

#include "hub75.hpp"

#ifdef BIG_PANEL
const uint32_t FB_WIDTH = 256;
const uint8_t FB_HEIGHT = 64;

const uint32_t GRID_WIDTH  = 128;
const uint32_t GRID_HEIGHT = 128;
#else
const uint32_t FB_WIDTH = 128;
const uint8_t FB_HEIGHT = 64;

const uint32_t GRID_WIDTH  = 128;
const uint32_t GRID_HEIGHT = 64;
#endif

constexpr float g = 7.f;
constexpr float k1 = 2.f;
constexpr float k2 = 3.f;

Pixel colormap[256];

void init_colormap() {
    for (int i=0; i<256; i++) {
        const float f = (float)i / 255.f;
//        colormap[i] = hsv_to_rgb(f / 3.f, 1.f, (1.f + 2.f * f) / 3.f);
        colormap[i] = hsv_to_rgb(0.2f + f / 2.f, 0.75f, 0.75f);
    }
}

void rand_init() {
    std::srand(std::time(nullptr));
    for (int i=0; i<15; i++) {
        (void)std::rand();
    }
}

float random_between(float minval, float maxval) {
    float v = (float)std::rand() / (float)RAND_MAX;
    return minval + v*(maxval - minval);
}

Hub75 hub75(FB_WIDTH, FB_HEIGHT, nullptr, PANEL_GENERIC, true);

void __isr dma_complete() {
    hub75.dma_complete();
}

unsigned char grid[2][GRID_WIDTH][GRID_HEIGHT];
int curr_buff = 0;

void clear_grid(int buff) {
    for (int i=0; i<GRID_WIDTH; i++) {
        for (int j=0; j<GRID_HEIGHT; j++) {
            grid[buff][i][j] = 0;
        }
    }
}


std::pair<unsigned int,unsigned int> grid_to_framebuffer(uint32_t x,uint32_t y,
                                     uint32_t grid_width, uint32_t grid_height,
                                     unsigned int fb_width, unsigned int fb_height) {
    unsigned int tx = (unsigned int)x;
    unsigned int ty = (unsigned int)y;
    if (ty < fb_height) {
        return std::make_pair(tx,ty);
    } else {
        tx = fb_width - 1 - tx;
        ty = (unsigned int)grid_height - 1 - ty;
        return std::make_pair(tx,ty);
    }
}

unsigned char sane_calc(const unsigned char old_val,
                        const float a,
                        const float b,
                        const float s) {
    const float new_val = (a/k1 + b/k2);
    return (unsigned char)std::max(0.f, std::min(255.f, new_val));
}

unsigned char infected_calc(const unsigned char old_val,
                            const float a,
                            const float b,
                            const float s) {
    const float new_val = (s/b + g);
    return (unsigned char)std::max(0.f, std::min(255.f, new_val));
}

int main() {

    stdio_init_all();

    hub75.start(dma_complete);
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(100);
    set_sys_clock_khz(250000, false);

    init_colormap();
    rand_init();

    int counter = 0;

    while (true) {

        hub75.clear();

        bool new_frame = false;

        const int prev_buff = !curr_buff;
        if (counter == 0) {
            printf("reset\n");
            new_frame = true;

            clear_grid(prev_buff);
            for (uint32_t x=0; x<GRID_WIDTH; x++) {
                for (uint32_t y=0; y<GRID_HEIGHT; y++) {
                    const float f = random_between(0.f,256.f);
                    const unsigned char v = (unsigned char)f;
//                    printf("%g -> %d ", f, (int)v);
                    grid[prev_buff][x][y] = v;
                }
//                printf("\n");
            }
            counter = 1000;
        } else {
            counter -= 1;
        }

        int num_sane = 0;
        int num_infected = 0;
        int num_ill = 0;
        for (uint32_t x=0; x<GRID_WIDTH; x++) {
            for (uint32_t y=0; y<GRID_HEIGHT; y++) {
                const unsigned char v = grid[prev_buff][x][y];
                if (v == 0) {
                    num_sane += 1;
                } else if (v < 255) {
                    num_ill += 1;
                } else {
                    num_infected += 1;
                }
            }
        }
        printf("sane = %d, infected = %d, ill = %d\n", num_sane, num_infected, num_ill);

        for (int x=0; x<GRID_WIDTH; x++) {
            for (int y=0; y<GRID_HEIGHT; y++) {
                float a = 0.f;
                float b = 0.f;
                float s = 0.f;
                for (int xo=-1; xo<=1; xo++) {
                    for (int yo=-1; yo<=1; yo++) {
                        const unsigned char v = grid
                            [prev_buff]
                            [(x+xo+GRID_WIDTH)%GRID_WIDTH]
                            [(y+yo+GRID_HEIGHT)%GRID_HEIGHT];
                        if (v == 0) {
                            ;
                        } else if (v < 255) {
                            a += 1.f;
                        } else {
                            b += 1.f;
                        }
                        s += (float)v;
                    }
                }

                const unsigned char old_val = grid[prev_buff][x][y];
                unsigned char new_val = 0;
                switch (old_val) {
                case 0:
                    // sane
                    new_val = sane_calc(old_val, a, b, s);
//                    if (new_frame) {
//                        printf("SANE: grid[%d][%d]\n",x,y);
//                        printf("  a = %g, b = %g, s = %g\n", a, b, s);
//                        printf("  %d -> %d\n", (int)old_val, (int)new_val);
//                    }
                    break;
                case 255:
                    // ill
                    break;
                default:
                    // infected
                    b += 1.f;
                    new_val = infected_calc(old_val, a, b, s);
                    if (new_frame) {
                        printf("INF:  grid[%d][%d]\n", x, y);
                        printf("  a = %g, b = %g, s = %g\n", a, b, s);
                        printf("  %d -> %d\n", (int)old_val, (int)new_val);
                    }
                    break;
                }
                grid[curr_buff][x][y] = new_val;
            }
        }

        for (uint32_t x=0; x<GRID_WIDTH; x++) {
            for (uint32_t y=0; y<GRID_HEIGHT; y++) {
                std::pair<uint,uint> fb_index = grid_to_framebuffer(x,y,
                                                                    GRID_WIDTH,GRID_HEIGHT,
                                                                    FB_WIDTH,FB_HEIGHT);
                const unsigned char v = grid[curr_buff][x][y];
                const Pixel p = colormap[v];
                hub75.set_color(fb_index.first,fb_index.second,p);
            }
        }

        hub75.flip(true); // Flip and clear to the background colour

        curr_buff = prev_buff;
        sleep_ms(5);
    }
}