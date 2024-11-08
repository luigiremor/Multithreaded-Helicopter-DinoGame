#include <iostream>
#include <pthread.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <ncurses.h>
#include <unistd.h>

// Scenario dimensions
const int WIDTH = 50;
const int HEIGHT = 20;

// Class to represent the helicopter
class Helicopter
{
public:
    std::atomic<int> x;
    std::atomic<int> y;
    std::atomic<int> remaining_missiles;
    std::mutex mtx;

    Helicopter(int startX, int startY, int capacity)
        : x(startX), y(startY), remaining_missiles(capacity) {}

    void move(int dx, int dy)
    {
        std::lock_guard<std::mutex> lock(mtx);
        x += dx;
        y += dy;
    }

    bool can_fire()
    {
        return remaining_missiles > 0;
    }

    void fire()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (remaining_missiles > 0)
            remaining_missiles--;
    }

    void reload(int amount)
    {
        std::lock_guard<std::mutex> lock(mtx);
        remaining_missiles += amount;
    }
};

// Class to represent a missile
class Missile
{
public:
    int x;
    int y;
    bool active;
    pthread_t th;

    Missile(int startX, int startY)
        : x(startX), y(startY), active(true), th(0) {}

    static void* move_wrapper(void* arg)
    {
        Missile* m = static_cast<Missile*>(arg);
        m->move();
        return nullptr;
    }

    void start()
    {
        pthread_create(&th, nullptr, Missile::move_wrapper, this);
    }

    void move()
    {
        while (active && x < WIDTH - 1)
        {
            usleep(50000); // Sleep for 50 milliseconds
            x++;
        }
        active = false;
    }

    void draw()
    {
        if (active)
        {
            mvprintw(y, x, "-");
        }
    }

    void join()
    {
        if (th)
        {
            pthread_join(th, nullptr);
            th = 0;
        }
    }
};

// Global variables
Helicopter heli(WIDTH / 2, HEIGHT / 2, 5); // Initial capacity of 5 missiles
std::vector<Missile*> missiles;
std::mutex mtx_missiles;
std::atomic<bool> running(true);

// Function to manage player input
void* thread_input(void* arg)
{
    int ch;
    nodelay(stdscr, TRUE); // Does not block waiting for input
    keypad(stdscr, TRUE);  // Captures special keys
    while (running)
    {
        ch = getch();
        switch (ch)
        {
        case KEY_UP:
        case 'w':
            if (heli.y > 1)
                heli.y--;
            break;
        case KEY_DOWN:
        case 's':
            if (heli.y < HEIGHT - 2)
                heli.y++;
            break;
        case KEY_LEFT:
        case 'a':
            if (heli.x > 1)
                heli.x--;
            break;
        case KEY_RIGHT:
        case 'd':
            if (heli.x < WIDTH - 2)
                heli.x++;
            break;
        case ' ':
            if (heli.can_fire())
            {
                heli.fire();
                // Create and start a new missile
                Missile* m = new Missile(heli.x + 1, heli.y);
                {
                    std::lock_guard<std::mutex> lock(mtx_missiles);
                    missiles.push_back(m);
                }
                m->start();
            }
            break;
        case 'q':
            running = false;
            break;
        default:
            break;
        }
        usleep(10000); // Sleep for 10 milliseconds
    }
    return nullptr;
}

// Function to render the scenario
void* thread_render(void* arg)
{
    while (running)
    {
        clear();
        // Draw borders
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
        mvprintw(heli.y.load(), heli.x.load(), "H");

        // Draw missiles
        {
            std::lock_guard<std::mutex> lock(mtx_missiles);
            for (auto it = missiles.begin(); it != missiles.end();)
            {
                if ((*it)->active)
                {
                    (*it)->draw();
                    ++it;
                }
                else
                {
                    // Join the thread and remove the missile from the list
                    (*it)->join();
                    delete *it;
                    it = missiles.erase(it);
                }
            }
        }

        // Show remaining missiles
        mvprintw(HEIGHT, 0, "Remaining missiles: %d", heli.remaining_missiles.load());

        refresh();
        usleep(50000); // Sleep for 50 milliseconds
    }
    return nullptr;
}

// Main function
int main()
{
    // Initialize ncurses
    initscr();
    noecho();
    curs_set(FALSE);

    // Create threads
    pthread_t input_thread_id, render_thread_id;
    pthread_create(&input_thread_id, nullptr, thread_input, nullptr);
    pthread_create(&render_thread_id, nullptr, thread_render, nullptr);

    // Wait for threads
    pthread_join(input_thread_id, nullptr);
    pthread_join(render_thread_id, nullptr);

    // End ncurses
    endwin();

    // Clear remaining missiles
    {
        std::lock_guard<std::mutex> lock(mtx_missiles);
        for (auto m : missiles)
        {
            m->active = false;
            m->join();
            delete m;
        }
        missiles.clear();
    }

    return 0;
}
