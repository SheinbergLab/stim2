# stim2
A cross platform system for visual stimulus presentation

## Design

stim2 is an OpenGL/GLES based program for showing graphics objects. It uses GLFW to open and initialize its display. The core role of the main program is to provide containers (graphics lists). These objects are created using "stimdlls", which define a large variety of graphics objects:

* metagroups
* polygons
* images
* motionpatches
* text
* svg objects
* user defined shaders with uniform controls
* spine2d animations
* video
* box2d worlds
* tilemap game environments

Examples of these are demonstrated through the stim2 development page, which is hosted by stim2 at [stim2-dev](http://localhost:4613/stim2-dev.html) and a terminal for interacting with the program is accessible at [terminal](http://localhost:4613/).

The system allows mixing and matching of objects in a single frame, and supports animation.

The system achieves frame accurate timing by running in C++, but configuration and frame callbacks are programming in Tcl, which provides high level access to the underlying graphics objects.

Extensive library support for numerical processing, curve and image creation, and physics computations are made available through the extensive dlsh/tcl packages that are available within any stim2 script.

