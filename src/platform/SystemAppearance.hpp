#pragma once

namespace galaxy {

// True when the operating system is set to a dark appearance. Where a system
// offers no setting to read (or it cannot be read), the answer is false.
// Safe to call from a worker thread, and it may take a few milliseconds, since
// some systems answer by running a small helper program.
bool systemPrefersDarkMode();

// Makes the window frame (title bar and menus the system draws) match the look the
// user picked: 0 follows the system, 1 is light, 2 is dark. Does nothing where the
// system does not allow it. Call from the main thread.
void applyNativeAppearance(int mode);

}  // namespace galaxy
