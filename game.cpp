#include <iostream>
#include <thread>
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
    std::thread th;

    Missile(int startX, int startY)
        : x(startX), y(startY), active(true) {}

    void start()
    {
        th = std::thread(&Missile::move, this);
    }

    void move()
    {
        while (active && x < WIDTH - 1)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
        if (th.joinable())
            th.join();
    }
};

// Global variables
Helicopter heli(WIDTH / 2, HEIGHT / 2, 5); // Initial capacity of 5 missiles
std::vector<Missile *> missiles;
std::mutex mtx_missiles;
std::atomic<bool> running(true);

// Function to manage player input
void thread_input()
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
                Missile *m = new Missile(heli.x + 1, heli.y);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Function to render the scenario
void thread_render()
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Main function
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
