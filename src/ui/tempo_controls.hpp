#pragma once

class TempoGrid;

// The song grid's time signature, tempo and snap division, for the timeline's toolbar.
// Returns true when the user changed it.
bool tempo_controls(TempoGrid& grid);
