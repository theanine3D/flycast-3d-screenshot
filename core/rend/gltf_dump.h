/*
	Copyright 2026 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

struct TA_context;

namespace gltfdump {

// Request a 3D screenshot of the next rendered frame.
// Thread-safe; the capture itself happens on the emulator thread.
void requestCapture();

// Called on the emulator thread after the TA context has been parsed by the renderer.
// Captures and saves the frame geometry as glTF if a capture was requested.
void checkCapture(TA_context *ctx);

}
