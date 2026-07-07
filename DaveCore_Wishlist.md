# DaveCore Feature Wishlist 🚀

This is the place to track all the "wouldn't it be cool if..." ideas for the Sovereign Live Performance Engine.

## UI & UX
- [ ] **Linear Rack View**: A clean, vertical stack of modules. No virtual patch cables.
- [ ] **Sleek Dark Mode**: Visual aesthetic inspired by high-end studio gear (glassmorphism, subtle glows).
- [ ] **Touch-Friendly Controls**: Large sliders and buttons for live tweaks on a touchscreen.

## MIDI & Integration
- [ ] **Dave-Speed MIDI Engine**: Ultra-low latency CC mapping for the Roland RD88.
- [ ] **Instant Song Switching**: Pre-fetch next song plugins in the background.
- [ ] **Hardware Abstraction**: Map physical knobs once, use them across all VSTs.

## Audio & Routing
- [ ] **Dual-Bus Mixing**: Independent FOH (Front of House) and IEM (In-Ear Monitor) mixes per slot.
- [ ] **VST3 Bridging**: Support for older VST2 plugins if needed.
- [ ] **Global FX Rack**: A master channel for buss compression and final EQ.

## Performance
- [ ] **High-Stability Mode**: Resource capping to prevent audio dropouts during sets.
- [ ] **Panic Button**: Instantly kill all MIDI notes and reset audio engine.

Things to know
1. RD88 will send Midi CC and Midi Note data. Note data goes to all VSTs. CC data only goes to one particular VST, and only specific CCs go to that VST (usually volume). All other CCs should be muted.
CC64 is the exception: it's the sustain pedal. It should go to all VSTs.
CC01 (modwheel) goes to the B3X.
I may need to add VSTs - but usually not.Most required VSTs are on this same computer we're developing on.
The B3X will need a note filter - I cut the top and bottom of the keyboard off.