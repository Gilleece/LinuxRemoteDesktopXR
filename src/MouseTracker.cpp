#include "MouseTracker.h"
#include <iostream>

MouseTracker::MouseTracker() {}

MouseTracker::~MouseTracker() {
    if (display) {
        XCloseDisplay(display);
    }
}

bool MouseTracker::init() {
    display = XOpenDisplay(nullptr);
    if (!display) {
        std::cerr << "Could not open X display for mouse tracking." << std::endl;
        return false;
    }
    
    root_window = DefaultRootWindow(display);
    return true;
}

bool MouseTracker::get_mouse_position(int& x, int& y) {
    if (!display) return false;
    
    Window root_return, child_return;
    int win_x, win_y;
    unsigned int mask_return;
    
    if (XQueryPointer(display, root_window, &root_return, &child_return,
                      &x, &y, &win_x, &win_y, &mask_return)) {
        return true;
    }
    
    return false;
}