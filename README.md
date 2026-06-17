# Wobbly Effect

A lightweight, zero-dependency Win32 desktop application that brings smooth, physics-based "jelly" animations to Windows OS windows when dragged.

## Showcase

https://github.com/user-attachments/assets/b5631596-1224-4fc7-b57f-cd43a92f9173





##  Features
* **Real-time Physics:** Driven by a discrete mass-spring-damper model using Hooke's law.
* * **GDI Rendering:** Uses native Win32 GDI APIs (`PlgBlt` affine transformations) running at 60 FPS.
* * **Background Persistence:** Overrides `WM_CLOSE` to minimize to the System Tray (`Shell_NotifyIcon`).
* * **Toggle GUI:** Features a minimalist interface to easily activate or deactivate the global mouse hook.

  

##  Compilation

Compile using GCC/MinGW with the following command to target the GUI subsystem and optimize performance:

```bash
g++ main.cpp -o "Wobbly Effect.exe" -luser32 -lgdi32 -lshell32 -mwindows -O3
 Usage
Launch the executable.

Click the toggle button to activate the effect.

Drag any standard window by its title bar to see the animation.

Close the main window to hide it into the System Tray, and right-click the icon to reopen or exit.
