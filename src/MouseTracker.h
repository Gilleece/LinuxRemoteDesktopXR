#ifndef MOUSETRACKER_H
#define MOUSETRACKER_H

extern "C" {
#include <X11/Xlib.h>
}

class MouseTracker {
public:
    MouseTracker();
    ~MouseTracker();
    
    bool init();
    bool get_mouse_position(int& x, int& y);
    
private:
    Display* display = nullptr;
    Window root_window;
};

#endif // MOUSETRACKER_H