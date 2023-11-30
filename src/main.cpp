#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"

#include "hub75.hpp"

//#define BLOCK 1
//#define BIG_PANEL 1
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

constexpr float g = 4.25f;
constexpr float k1 = 1.6f;
constexpr float k2 = 2.2f;
constexpr float k3 = 0.125f;

Pixel colormap[256];

void init_colormap() {
    colormap[0] = Pixel(0,0,0);
    for (int i=1; i<256; i++) {
        const float f = (float)i / 255.f;
        colormap[i] = hsv_to_rgb(0.4f + f * 0.375, 0.875f, f);
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

std::pair<uint32_t,uint32_t> rand_range(uint32_t w) {
#ifdef BLOCK
    uint32_t v1 = (uint32_t)random_between(0.f, (float)(w-1));
    uint32_t v2 = (uint32_t)random_between(0.f, (float)(w-1));

    if (v1 < v2) {
        return std::make_pair(v1,v2);
    } else {
        return std::make_pair(v2,v1);
    }
#else
    return std::make_pair(0,w-1);
#endif
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

unsigned char to_byte(float v) {
    return (unsigned char)std::max(0.f, std::min(255.f, v));
}

unsigned char sane_calc(const unsigned char old_val,
                        const int a,
                        const int b,
                        const float s) {
    const float new_val = ((float)a/k1 + (float)b/k2);
    return to_byte(new_val);
}

unsigned char infected_calc(const unsigned char old_val,
                            const int a,
                            const int b,
                            const float s) {
    const float new_val = (k3*s/(float)b + g);
    return to_byte(new_val);
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

    const int probe_x = -1;
    const int probe_y = -1;
    while (true) {

        hub75.clear();

        bool new_frame = false;

        const int prev_buff = !curr_buff;
        if (counter == 0) {
            printf("reset\n");
            new_frame = true;

            clear_grid(prev_buff);
            std::pair<uint32_t,uint32_t> xrange = rand_range(GRID_WIDTH);
            std::pair<uint32_t,uint32_t> yrange = rand_range(GRID_HEIGHT);
            for (uint32_t x=xrange.first; x<= xrange.second; x++) {
                for (uint32_t y=yrange.first; y<=yrange.second; y++) {
                    const float f = random_between(0.f,255.f);
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
                int a = 0;
                int b = 0;
                float s = 0.f;
                for (int xo=-1; xo<=1; xo++) {
                    for (int yo=-1; yo<=1; yo++) {
                        const unsigned char v = grid
                            [prev_buff]
                            [(x+xo+GRID_WIDTH)%GRID_WIDTH]
                            [(y+yo+GRID_HEIGHT)%GRID_HEIGHT];
                        if (x != 0 || y != 0) {
                            if (v == 0) {
                                ;
                            } else if (v < 255) {
                                a += 1;
                            } else {
                                b += 1;
                            }
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
                    if (new_frame) {
//                        printf("SANE: grid[%d][%d]\n",x,y);
//                        printf("  a = %d, b = %d, s = %g\n", a, b, s);
//                        printf("  %d -> %d\n", (int)old_val, (int)new_val);
                    }
                    if (x == probe_x && y == probe_y) {
                        printf("SANE(%d,%d,%f) -> %d\n",a,b,s,(int)new_val);
                    }
                    break;
                case 255:
                    // ill
                    if (x == probe_x && y == probe_y) {
                        printf("ILL -> %d\n",(int)new_val);
                    }
                    break;
                default:
                    // infected
                    b += 1;
                    new_val = infected_calc(old_val, a, b, s);
//                    if (new_frame) {
//                        printf("INF:  grid[%d][%d]\n", x, y);
//                        printf("  a = %d, b = %d, s = %g\n", a, b, s);
//                        printf("  %d -> %d\n", (int)old_val, (int)new_val);
//                    }
                    if (x == probe_x && y == probe_y) {
                        printf("INFECTED(%d,%d,%f) -> %d\n",a,b,s,(int)new_val);
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
        sleep_ms(1);
    }
}
