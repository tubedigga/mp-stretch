Tubedigga
MP Stretch
Version 1.6.7
AKAI S-Series Rack Sampler Style Time-stretching insert effect for
AKAI MPC and AKAI FORCE standalone hardware.
https://www.tubedigga.com tubedigga@gmail.com
MP Stretch 1.6.7 2
Introduction and Key Features
MP Stretch is a stereo insert effect for the Akai MPC developed by Tubedigga.
It is a VST2-style plugin for MPC OS providing a time-stretch effect much closer to the AKAI Professional
S-Series rack samplers from the 1990s in comparison to other warping or stretching effects available in the
MPC OS to date.
You may load it into any insert effect slot for any pad, program or output in your MPC or Force hardware.
You can also load MP Stretch onto one or more of the four sampler insert effect slots for live audio input
processing and recording.
Automation for all parameters can be recorded into the MPC/FORCE sequencer.
Compatibility
MP Stretch is built for MPC OS 3.10.1 and later.
To run the insert effect, you must check the boxes for ENABLE ENHANCEMENTS AND EXTERNAL
PLUGIN SUPPORT which can be found in your MPC/Force MENU/PREFERENCES/ENHANCEMENTS
section, near the very bottom of the page.
MP Stretch has been confirmed in use on an MPC Live III but should work on other Gen 1 and Gen 2 AKAI
MPC and Force devices running the same firmware with the aforementioned settings enabled.
Installation
On the root of a USB stick, USB drive, SD card or SSD drive, create a new folder and rename it to VST2
Plugins.
NOTE: This drive must be inserted into your MPC/Force hardware any time you wish to use this or any
other effect you choose to store on it.
Next copy the folder named Tubedigga - VST - MP Stretch into the VST2 Plugins folder.
MP Stretch Interface (single page/tab)
From left to right you will see Cyclic, Length, Delay, Phase, Pitch, and Rate.
A readout in the screen shows the current value of each parameter.
Double-tap a knob to expand the encoder view.
Press and hold shift to adjust in finer increments for more precision if necessary.
CYCLIC, LENGTH, DELAY and PHASE are adjustable via Q-Links by default providing your QLINK
mode is set to SCREEN or another custom modes that you have assigned to control these parameters. .
You may choose other modes such as PROJECT or TRACK and configure/reconfigure what physical
controls can modify these parameters in QLINK EDIT MODE. This can be anything from the touch strip, the
MPCE pads or the AKAI Force crossfader for example. Some interesting results may be obtained using the
ENVELOPE FOLLOWER.
NOTE: Pitch and Rate have no Q-Link control by default.
MP Stretch 1.6.7 3
PARAMETERS
Cyclic
Cyclic is the grain size, in samples. The display runs from 0 to 2000.
CYCLIC’s default value is 400, which reads 400smp.
Cyclic will have no effect on the sound if LENGTH is at 100 but any value that exceeds this will. Cyclic will
also not take any effect if the delay parameter is set to anything above zero.
Length
Length runs from 100% to 2000% and is the main parameter that engages stretching. By default, its value is
100 where no stretching occurs.
Anything beyond this value and the stretch effect can be heard, with the next value being 102. Press and hold
SHIFT to adjust in finer increments of a value of 2 instead of the larger default increments of 19.
Please experiment by offsetting both the CYCLIC and LENGTH parameters for those classic robotic and
metallic effects that were exploited heavily in 90s jungle music.
With Pitch off, Length is how long the audio becomes, and the pitch of whatever you are applying MP
Stretch to stays where it was.
100% keeps the original duration and no stretching takes effect.
A higher value makes the sound longer.
How this relates to pitch: With the Pitch setting on, Length becomes the pitch ratio, and the duration stays
with the beat.
200% is one octave up. The control runs the full distance to 2000%.
Delay
The DELAY parameter allows you to offset when stretching is applied to a sound so you can easily trigger a
sample/synth etc with your pads, MPC Key 37/61 keys, or an external midi keyboard without having to
simultaneously adjust the LENGTH parameter. Set your length first, and delay will provide the convenient
gap so you don’t end up stretching silence - which is not very interesting in my experience.
Delay waits for a hit, then waits for the time specified by adjusting its control knob to the desired value in
milliseconds, and only while Length is above 100%.
If the hit ends during the wait, nothing is stretched.
This is very useful when flattening a pad to ‘bake’ the MP Stretch effect into a new sample, or to select a
region in the sample that occurs further in.
Delay runs from 0 to 2000 milliseconds and opens at 0, which reads 0ms.
At 0, Delay leaves the stretch path as it is.
Bring the other controls back to their default values and the sound returns to the live input.
MP Stretch 1.6.7 4
Phase
Phase runs from 0 to 30 milliseconds. The default value is off.
At 0 the readout shows off, and the delay is out of the signal.
Above 0 the readout shows the time in milliseconds, with tenths below 10 ms.
Phase is a short delay with 50% feedback: half of the delayed signal is fed back, so a bright hit can ring.
While Length is at 100% and Cyclic is at its default value, Phase delays the dry signal.
When a stretch is running, Phase delays that stretched sound. Turn the control and the delay time glides to the
new value.
Pitch
Pitch is an on/off switch.
There is no pitch percentage on this control, but pitch will increase with higher length values applied.
Off keeps the original pitch and lets Length change the duration.
On keeps the duration with the beat and uses Length as the pitch.
200% is one octave up. 100% is the original pitch, with the switch either way.
Rate
Rate allows you to adjust the audio sample rate for a grainier, more vintage sound.
Rate runs from 22500 to 44100 Hz and opens at 44100, which reads 44100Hz.
That setting is full band.
Licence
MP Stretch is released under the GNU General Public License, version 3.0 (GPL-3.0). If you have the
software, you may keep it, change it, and share it under GPL-3.0. Anyone you pass it to receives the same
permission.
The time-stretch code is modified from https://github.com/dar-io-p/potenza-time-stretch (GPL-3.0, dar-io-p).
Selling a download does not stop someone who has the folder from copying it. The folder has no activation
code and no lock.
VST is a trademark of Steinberg Media Technologies GmbH. MPC is an Akai product.
Support
MP Stretch. 2026 TUBEDIGGA
https://www.tubedigga.com
tubedigga@gmail.com for any enquiries.
