#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <ncurses.h>
#include <unistd.h>

// Scene dimensions
const int WIDTH = 50;
const int HEIGHT = 20;

// Initial position of the helicopter
std::atomic<int> heli_x(WIDTH / 2);
std::atomic<int> heli_y(HEIGHT / 2);

std::mutex mtx;
std::atomic<bool> running(true);

void thread_input()
{
    int ch;
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    while (running)
    {
        ch = getch();
        mtx.lock();
        switch (ch)
        {
        case KEY_UP:
        case 'w':
            if (heli_y > 1)
                heli_y--;
            break;
        case KEY_DOWN:
        case 's':
            if (heli_y < HEIGHT - 2)
                heli_y++;
            break;
        case KEY_LEFT:
        case 'a':
            if (heli_x > 1)
                heli_x--;
            break;
        case KEY_RIGHT:
        case 'd':
            if (heli_x < WIDTH - 2)
                heli_x++;
            break;
        case 'q':
            running = false;
            break;
        default:
            break;
        }
        mtx.unlock();
        usleep(10000);
    }
}

void thread_render()
{
    while (running)
    {
        mtx.lock();
        clear();

        for (int i = 0; i < WIDTH; i++)
        {
            mvprintw(0, i, "#");
            mvprintw(HEIGHT - 1, i, "#");
        }
        for (int i = 0; i < HEIGHT; i++)
        {
            mvprintw(i, 0, "#");
            mvprintw(i, WIDTH - 1, "#");
        }
        // Draw helicopter
        mvprintw(heli_y.load(), heli_x.load(), "H");
        refresh();
        mtx.unlock();
        usleep(50000);
    }
}

int main()
{
    // Initialize ncurses
    initscr();
    noecho();
    curs_set(FALSE);

    // Create threads
    std::thread input_thread(thread_input);
    std::thread render_thread(thread_render);

    // Wait for threads
    input_thread.join();
    render_thread.join();

    // Finalize ncurses
    endwin();

    return 0;
}
