// A synthetic room (an L-shaped living room with furniture, door, window and a scanned-looking global mesh) for the
// demo mode (a file named "demo" in the app's files folder) and the PC renderer. Not anyone's real room.
#pragma once

#include "state.h"

// Fills the FB and META room sets and one saved anchor; `stage_in_base` places the room's STAGE frame (floor at
// y = 0) in the app's base space.
void demo_room(MrState &st, const Pose &stage_in_base);
// Runtime answers for the PC renderer (extensions, system properties): what a Quest 3 would list.
void demo_runtime(MrState &st);
